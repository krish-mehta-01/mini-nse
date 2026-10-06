"""Turns one stock from an NSE pre-open snapshot into a morning file the engine can replay.

NSE publishes the pre-open book as total quantity per price level (not individual orders), plus the total
of market orders ("ATO", at the open) per side. Each level becomes one synthetic limit order and each ATO
total one market order. The arrival order of these synthetic orders is unknown, but it only affects *who*
fills, never the price or the quantity, which is what gets compared.

NSE shows only about 10 levels around the indicative price. When the levels shown don't add up to NSE's
total buy/sell quantity, some orders are hidden. `hidden` decides what to do with them:
  "ignore"      leave them out ("visible only")
  "aggressive"  add them as market orders, i.e. assume they would trade at any price shown
"""
from decimal import Decimal


def paise(value):
    """NSE sends prices as JSON numbers like 1171.2; Decimal(str(...)) keeps them exact."""
    exact = Decimal(str(value)) * 100
    if exact != exact.to_integral_value():
        raise ValueError(f"price {value} has more than two decimals")
    return int(exact)


def book_from(entry):
    """The parts of one stock's snapshot entry that matter, with prices in paise."""
    market = entry["detail"]["preOpenMarket"]
    levels = [(paise(l["price"]), l["buyQty"], l["sellQty"]) for l in market["preopen"]]
    ato_buy, ato_sell = market.get("atoBuyQty") or 0, market.get("atoSellQty") or 0
    hidden_buy = market["totalBuyQuantity"] - ato_buy - sum(b for _, b, _ in levels)
    hidden_sell = market["totalSellQuantity"] - ato_sell - sum(s for _, _, s in levels)
    return {
        "symbol": entry["metadata"]["symbol"],
        "series": entry["metadata"].get("series", ""),
        "prev_close": paise(market["prevClose"]),
        "levels": levels,
        "ato_buy": ato_buy,
        "ato_sell": ato_sell,
        "hidden_buy": max(hidden_buy, 0),
        "hidden_sell": max(hidden_sell, 0),
        "full_book": hidden_buy == 0 and hidden_sell == 0,
        "iep": paise(market["IEP"]) if market.get("IEP") else None,
        "final_price": paise(market["finalPrice"]) if market.get("finalPrice") else None,
        "final_quantity": market.get("finalQuantity") or 0,
        "last_update": market.get("lastUpdateTime", ""),
    }


def fmt(p):
    return f"{p // 100}.{p % 100:02d}"


def morning_text(book, hidden="ignore", note=""):
    lines = [f"# NSE pre-open book: {book['symbol']} {note}".rstrip(), f"PREV_CLOSE {fmt(book['prev_close'])}",
             "PHASE PREOPEN"]
    market_buy, market_sell = book["ato_buy"], book["ato_sell"]
    if hidden == "aggressive":
        market_buy += book["hidden_buy"]
        market_sell += book["hidden_sell"]
    if market_buy:
        lines.append(f"ADD 1 BUY MARKET {market_buy}")
    if market_sell:
        lines.append(f"ADD 2 SELL MARKET {market_sell}")
    for k, (price, buy, sell) in enumerate(book["levels"]):
        if buy:
            lines.append(f"ADD {1000 + k} BUY LIMIT {fmt(price)} {buy}")
        if sell:
            lines.append(f"ADD {2000 + k} SELL LIMIT {fmt(price)} {sell}")
    lines.append("PHASE AUCTION")
    return "\n".join(lines) + "\n"
