"""A slow, simple Python version of the whole engine, written separately from the C++ to check it.

It favours being obviously right over being fast: the auction tries every candidate price with plain sums,
and the order book is a list that gets re-sorted whenever it's needed. It prints exactly the same lines as
mini_nse_replay (see apps/replay.cpp), so the two can be compared line by line.

    python3 tools/reference.py FILE...
"""
import sys

PHASES = ["CLOSED", "PREOPEN", "LIMIT_ONLY", "AUCTION", "CONTINUOUS"]
ALLOWED = {("CLOSED", "PREOPEN"), ("PREOPEN", "LIMIT_ONLY"), ("PREOPEN", "AUCTION"), ("LIMIT_ONLY", "AUCTION"),
           ("AUCTION", "CONTINUOUS")}


def parse_price(text):
    whole, _, frac = text.partition(".")
    if not whole.isdigit() or len(frac) > 2 or ("." in text and not frac) or (frac and not frac.isdigit()):
        raise ValueError(f"bad price {text}")
    return int(whole) * 100 + int(frac.ljust(2, "0") or 0)


def fmt(price):
    return f"{price // 100}.{price % 100:02d}"


def read_script(path):
    config = {"prev": 0, "tick": 1, "low": None, "high": None}
    events = []
    with open(path, encoding="utf-8") as f:
        for number, raw in enumerate(f, 1):
            w = raw.split("#")[0].split()
            if not w:
                continue
            if w[0] == "PREV_CLOSE":
                config["prev"] = parse_price(w[1])
            elif w[0] == "TICK":
                config["tick"] = parse_price(w[1])
            elif w[0] == "BAND":
                config["low"], config["high"] = parse_price(w[1]), parse_price(w[2])
            elif w[0] == "PHASE":
                events.append((number, "PHASE", w[1]))
            elif w[0] == "ADD":
                order = {"id": int(w[1]), "side": w[2], "type": w[3]}
                if w[3] == "LIMIT":
                    order["price"], order["qty"] = parse_price(w[4]), int(w[5])
                else:
                    order["price"], order["qty"] = 0, int(w[4])
                events.append((number, "ADD", order))
            elif w[0] == "CANCEL":
                events.append((number, "CANCEL", int(w[1])))
            elif w[0] == "MODIFY":
                events.append((number, "MODIFY", (int(w[1]), parse_price(w[2]), int(w[3]))))
            else:
                raise ValueError(f"line {number}: unknown command {w[0]}")
    return config, events


# ---------- the opening auction ----------

def demand_supply(orders, p):
    demand = sum(o["qty"] for o in orders if o["side"] == "BUY" and (o["type"] == "MARKET" or o["price"] >= p))
    supply = sum(o["qty"] for o in orders if o["side"] == "SELL" and (o["type"] == "MARKET" or o["price"] <= p))
    return demand, supply


def equilibrium(orders, prev):
    """Returns (price or None, quantity, imbalance, rule)."""
    candidates = sorted({o["price"] for o in orders if o["type"] == "LIMIT"})
    if not candidates:
        mb = sum(o["qty"] for o in orders if o["side"] == "BUY")
        ms = sum(o["qty"] for o in orders if o["side"] == "SELL")
        return (prev, min(mb, ms), abs(mb - ms), "MarketOrdersOnly") if mb > 0 and ms > 0 else (None, 0, 0, "NoPrice")
    rows = []
    for p in candidates:
        d, s = demand_supply(orders, p)
        rows.append((p, min(d, s), abs(d - s)))
    best = max(r[1] for r in rows)
    if best == 0:
        return None, 0, 0, "NoPrice"
    rows = [r for r in rows if r[1] == best]
    if len(rows) == 1:
        return rows[0] + ("MaxTradable",)
    least = min(r[2] for r in rows)
    rows = [r for r in rows if r[2] == least]
    if len(rows) == 1:
        return rows[0] + ("MinImbalance",)
    nearest = min(abs(r[0] - prev) for r in rows)
    rows = [r for r in rows if abs(r[0] - prev) == nearest]
    if len(rows) == 1:
        return rows[0] + ("NearestPreviousClose",)
    d, s = demand_supply(orders, prev)
    return prev, min(d, s), abs(d - s), "HalfwayPreviousClose"


def fill_key(o):
    better_price = -o["price"] if o["side"] == "BUY" else o["price"]
    return (0, 0, o["seq"]) if o["type"] == "MARKET" else (1, better_price, o["seq"])


def run_auction(orders, prev):
    price, qty, imbalance, rule = equilibrium(orders, prev)
    remaining = [dict(o) for o in orders]
    trades = []
    if price is not None:
        def willing(side):
            return sorted((o for o in remaining if o["side"] == side and (
                o["type"] == "MARKET" or (o["price"] >= price if side == "BUY" else o["price"] <= price))), key=fill_key)
        buys, sells = willing("BUY"), willing("SELL")
        while buys and sells:
            b, s = buys[0], sells[0]
            q = min(b["qty"], s["qty"])
            trades.append((b["id"], s["id"], price, q))
            b["qty"] -= q
            s["qty"] -= q
            if b["qty"] == 0:
                buys.pop(0)
            if s["qty"] == 0:
                sells.pop(0)
    carry = price if price is not None else prev
    leftovers = []
    for o in remaining:
        if o["qty"] > 0:
            if o["type"] == "MARKET":
                o["type"], o["price"] = "LIMIT", carry
            leftovers.append(o)
    return (price, qty, imbalance, rule), trades, leftovers


# ---------- the normal market ----------

class Market:
    def __init__(self):
        self.orders = []  # resting limit orders, any order; sorted whenever priority matters
        self.next_seq = 1

    def side(self, side):
        key = (lambda o: (-o["price"], o["seq"])) if side == "BUY" else (lambda o: (o["price"], o["seq"]))
        return sorted((o for o in self.orders if o["side"] == side), key=key)

    def find(self, oid):
        return next((o for o in self.orders if o["id"] == oid), None)

    def submit(self, order):
        order = dict(order, seq=self.next_seq)
        self.next_seq += 1
        trades = []
        other = "SELL" if order["side"] == "BUY" else "BUY"
        while order["qty"] > 0:
            queue = self.side(other)
            if not queue:
                break
            best = queue[0]
            if order["type"] == "LIMIT" and (best["price"] > order["price"] if order["side"] == "BUY"
                                             else best["price"] < order["price"]):
                break
            q = min(order["qty"], best["qty"])
            buy, sell = (order, best) if order["side"] == "BUY" else (best, order)
            trades.append((buy["id"], sell["id"], best["price"], q))
            order["qty"] -= q
            best["qty"] -= q
            if best["qty"] == 0:
                self.orders.remove(best)
        if order["qty"] > 0 and order["type"] == "LIMIT":
            self.orders.append(order)
        return trades

    def restore(self, order):
        self.orders.append(dict(order))
        self.next_seq = max(self.next_seq, order["seq"] + 1)


# ---------- the session ----------

def replay(path):
    config, events = read_script(path)
    out = []
    phase = "CLOSED"
    used = set()
    pre = []  # pre-open orders in arrival order
    pre_seq = [1]
    market = Market()
    auction_leftovers = []

    def price_reject(price):
        if price % config["tick"] != 0:
            return "OffTick"
        if (config["low"] is not None and price < config["low"]) or (config["high"] is not None and price > config["high"]):
            return "OutsideBand"
        return None

    def pre_find(oid):
        return next((o for o in pre if o["id"] == oid), None)

    for line, kind, arg in events:
        reject, trades, stage = None, [], "C"
        preopen = phase in ("PREOPEN", "LIMIT_ONLY")
        if kind == "PHASE":
            if (phase, arg) not in ALLOWED:
                reject = "BadTransition"
            else:
                phase = arg
                if arg == "AUCTION":
                    (price, qty, imbalance, rule), trades, auction_leftovers = run_auction(pre, config["prev"])
                    out.append(f"AUCTION {fmt(price) if price is not None else 'none'} {qty} {imbalance} {rule}")
                    stage = "A"
                elif arg == "CONTINUOUS":
                    for o in auction_leftovers:
                        market.restore(o)
        elif kind == "ADD":
            o = arg
            if not preopen and phase != "CONTINUOUS":
                reject = "WrongPhase"
            elif o["qty"] <= 0 or (o["type"] == "LIMIT" and o["price"] <= 0):
                reject = "InvalidOrder"
            elif o["id"] in used:
                reject = "DuplicateId"
            elif o["type"] == "MARKET" and phase == "LIMIT_ONLY":
                reject = "MarketOrdersClosed"
            elif o["type"] == "LIMIT" and price_reject(o["price"]):
                reject = price_reject(o["price"])
            else:
                used.add(o["id"])
                if preopen:
                    pre.append(dict(o, seq=pre_seq[0]))
                    pre_seq[0] += 1
                else:
                    trades = market.submit(o)
        elif kind == "CANCEL":
            if preopen:
                o = pre_find(arg)
                if o is None:
                    reject = "UnknownOrder"
                elif o["type"] == "MARKET" and phase == "LIMIT_ONLY":
                    reject = "MarketOrdersClosed"
                else:
                    pre.remove(o)
            elif phase == "CONTINUOUS":
                o = market.find(arg)
                if o is None:
                    reject = "UnknownOrder"
                else:
                    market.orders.remove(o)
            else:
                reject = "WrongPhase"
        elif kind == "MODIFY":
            oid, price, qty = arg
            o = pre_find(oid) if preopen else market.find(oid) if phase == "CONTINUOUS" else None
            is_limit = o is not None and o["type"] == "LIMIT"
            if not preopen and phase != "CONTINUOUS":
                reject = "WrongPhase"
            elif o is None:
                reject = "UnknownOrder"
            elif qty <= 0 or (is_limit and price <= 0):
                reject = "InvalidOrder"
            elif not is_limit and phase == "LIMIT_ONLY":
                reject = "MarketOrdersClosed"
            elif is_limit and price_reject(price):
                reject = price_reject(price)
            else:
                new_price = price if is_limit else o["price"]
                keeps_place = new_price == o["price"] and qty <= o["qty"]
                if keeps_place:
                    o["qty"] = qty
                elif preopen:
                    pre.remove(o)
                    pre.append(dict(o, price=new_price, qty=qty, seq=pre_seq[0]))
                    pre_seq[0] += 1
                else:
                    market.orders.remove(o)
                    trades = market.submit(dict(o, price=new_price, qty=qty))
        if reject:
            out.append(f"REJECT {line} {reject}")
        for b, s, p, q in trades:
            out.append(f"TRADE {stage} {b} {s} {fmt(p)} {q}")

    def waiting(o):
        what = "MARKET" if o["type"] == "MARKET" else f"LIMIT {fmt(o['price'])}"
        return f"BOOK {o['side']} {o['id']} {what} {o['qty']}"

    if phase == "CONTINUOUS":
        out += [waiting(dict(o, type="LIMIT")) for side in ("BUY", "SELL") for o in market.side(side)]
    elif phase == "AUCTION":
        out += [waiting(o) for o in auction_leftovers]
    else:
        out += [waiting(o) for o in pre]
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(f"=== {path}")
        print("\n".join(replay(path)))
