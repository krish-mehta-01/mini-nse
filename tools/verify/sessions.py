"""Random trading mornings in the replay file format, for testing and benchmarking.

The prices sit on a small grid around the previous close, so ties, every tie-break rule, market orders,
partial fills, cancels, modifies and every kind of reject all happen often.

    python3 tools/verify/sessions.py --seed 7                  # print one random morning
    python3 tools/verify/sessions.py --benchmark > morning.txt  # a big realistic one for timing
"""
import argparse
import random


def fmt(paise):
    return f"{paise // 100}.{paise % 100:02d}"


def morning(seed, orders=40):
    rng = random.Random(seed)
    tick = rng.choice([1, 5])
    prev = 10000 + rng.choice([0, 0, 5])  # 100.05 sits halfway between grid prices, so the halfway rule fires
    lines = [f"# random morning, seed {seed}", f"PREV_CLOSE {fmt(prev)}", f"TICK {fmt(tick)}"]
    if rng.random() < 0.3:
        lines.append(f"BAND {fmt(prev - 50)} {fmt(prev + 50)}")

    next_id = [1]
    live = []  # ids that might still be waiting
    market_share = 1.0 if rng.random() < 0.05 else 0.15  # now and then, a pre-open of market orders only

    def price():
        p = 10000 + 10 * rng.randint(-6, 6)  # grid of 10 paise, so many orders share a price
        return p + rng.choice([1, 2]) if rng.random() < 0.03 else p  # now and then, off the tick

    def quantity():
        return rng.choice([0, -5]) if rng.random() < 0.02 else rng.randint(1, 400)

    def action():
        roll = rng.random()
        if roll < 0.62 or not live:
            oid = rng.choice(live) if live and rng.random() < 0.02 else next_id[0]  # sometimes a duplicate id
            next_id[0] += 1
            live.append(oid)
            side = rng.choice(["BUY", "SELL"])
            if rng.random() < market_share:
                return f"ADD {oid} {side} MARKET {quantity()}"
            return f"ADD {oid} {side} LIMIT {fmt(price())} {quantity()}"
        target = rng.choice(live) if rng.random() < 0.9 else 999999  # sometimes an unknown id
        if roll < 0.82:
            if target in live:
                live.remove(target)
            return f"CANCEL {target}"
        return f"MODIFY {target} {fmt(price())} {quantity()}"

    def stretch(n):
        lines.extend(action() for _ in range(n))

    phase_order = ["PREOPEN", "LIMIT_ONLY", "AUCTION", "CONTINUOUS"]
    if rng.random() < 0.05:
        lines.append(f"ADD {next_id[0]} BUY LIMIT 100.00 10")  # before 9:00: refused
        next_id[0] += 1
    lines.append("PHASE PREOPEN")
    stretch(rng.randint(0, orders))
    if rng.random() < 0.75:
        lines.append("PHASE LIMIT_ONLY")
        stretch(rng.randint(0, orders // 3))
    if rng.random() < 0.05:
        lines.append(f"PHASE {rng.choice(phase_order)}")  # a phase out of order: refused
    lines.append("PHASE AUCTION")
    if rng.random() < 0.1:
        stretch(2)  # during the buffer: refused
    if rng.random() < 0.9:
        lines.append("PHASE CONTINUOUS")
        market_share = 0.15
        stretch(rng.randint(0, orders))
    return "\n".join(lines) + "\n"


def benchmark_morning(seed, preopen=5000, continuous=200000):
    """A big, realistic morning for timing: mostly valid orders around Rs 100 on a 5-paise tick,
    with cancels and modifies of orders that are really waiting, and market orders that sweep levels."""
    rng = random.Random(seed)
    lines = [f"# benchmark morning, seed {seed}", "PREV_CLOSE 100.00", "TICK 0.05", "PHASE PREOPEN"]
    live, next_id = [], 1

    def price(spread):
        return 10000 + 5 * rng.randint(-spread, spread)

    def stretch(n, spread, market_share, cancel_share, modify_share):
        nonlocal next_id
        for _ in range(n):
            roll = rng.random()
            if live and roll < cancel_share:
                lines.append(f"CANCEL {live.pop(rng.randrange(len(live)))}")
            elif live and roll < cancel_share + modify_share:
                lines.append(f"MODIFY {rng.choice(live)} {fmt(price(spread))} {rng.randint(1, 500)}")
            else:
                side = rng.choice(["BUY", "SELL"])
                if rng.random() < market_share:
                    lines.append(f"ADD {next_id} {side} MARKET {rng.randint(1, 300)}")
                else:
                    lines.append(f"ADD {next_id} {side} LIMIT {fmt(price(spread))} {rng.randint(1, 500)}")
                    live.append(next_id)
                next_id += 1

    stretch(preopen, 40, 0.05, 0.10, 0.05)
    lines.append("PHASE LIMIT_ONLY")
    lines.append("PHASE AUCTION")
    lines.append("PHASE CONTINUOUS")
    stretch(continuous, 20, 0.08, 0.30, 0.10)
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--orders", type=int, default=40, help="roughly how many events per stretch")
    parser.add_argument("--benchmark", action="store_true", help="a big realistic morning for timing instead")
    args = parser.parse_args()
    print(benchmark_morning(args.seed) if args.benchmark else morning(args.seed, args.orders), end="")
