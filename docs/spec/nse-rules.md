# NSE pre-open call auction: rules spec

This is the spec for the Mini NSE engine. Every rule has a source, and every engine test should trace back to a
rule here. Rules marked ⚠️ are not confirmed from an NSE document yet, so the engine should make them easy to
change (a config flag or one isolated function).

Drafted 2026-10-05. Covers the framework in force since **7 September 2026**.

---

## 1. Session timeline (cash market, every trading day)

| Time (IST) | Phase | What's allowed |
|---|---|---|
| 09:00 – 09:05 | Order entry, part I | Add, modify, cancel **market and limit** orders |
| 09:05 – close | Order entry, part II | Add, modify, cancel **limit orders only**. New market orders are rejected; existing market orders can't be modified or cancelled |
| close = random time in **09:08 – 09:10** | Random close | System closes order entry at an unannounced moment, so nobody can time orders for "the last second" |
| 09:10 – 09:12 | Matching | Equilibrium price found, orders matched, trades confirmed |
| 09:12 – 09:15 | Buffer | No order entry; unmatched orders move to the normal market |
| 09:15 | Normal market | Continuous price-time trading starts |

Before 7 Sep 2026: order entry 09:00–09:08 with random close in the last minute, matching 09:08–09:12, and no
market-order cutoff. The equilibrium price rules did **not** change.
Sources: [S2], [S3], [S4], [S5].

**Engine design:** phases come from `PHASE` events in the input file, never from the real clock. The random close
is just "the input file says order entry closed here", so replays stay deterministic.

## 2. Orders allowed

- Limit orders and market orders (market orders only until 09:05).
- **Not allowed:** stop-loss (SL), immediate-or-cancel (IOC), disclosed quantity (DQ). [S3]
- Algo market orders only in the first 5 minutes. [S3]
- Trades executed in pre-open are final. [S4]
- ⚠️ Price band and tick size checks on pre-open orders: not confirmed. Assume orders must be a multiple of the
  stock's tick size and inside its price band, and reject otherwise.

## 3. Equilibrium price (the opening price)

For every candidate price `p` (each distinct limit price in the book):

- **demand(p)** = all market buys + limit buys with price **≥ p**
- **supply(p)** = all market sells + limit sells with price **≤ p**
- **tradable(p)** = min(demand, supply)
- **imbalance(p)** = |demand − supply|

Pick the equilibrium price with these rules, applying each one only if the one before leaves a tie: [S1], [S2]

1. **Maximum tradable quantity.**
2. **Minimum imbalance** (absolute value).
3. **Closest to the previous day's close.** If the previous close is exactly halfway between the two closest
   candidates, the **previous close itself** becomes the equilibrium price. After a corporate action, use the
   adjusted close or base price.

Special cases: [S1]
- **Only market orders on both sides:** they match at the previous close (or adjusted close/base price).
- **No price discovered** (tradable = 0 everywhere, e.g. best buy price < best sell price and no market orders):
  no pre-open trades; the first trade in the normal market sets the open price.

The equilibrium price becomes the day's **open price**. [S1]

⚠️ Is the previous close itself a candidate price in rule 3 generally, or only in the halfway case? NSE's wording
only mentions it for halfway. Implement "candidates = limit prices" plus the halfway rule.

## 4. Matching and allocation

Since 7 Sep 2026, **market orders get priority over limit orders.** [S3], [S4]

Matching sequence, all at the single equilibrium price: [S3]
1. Market buys vs market sells, by time priority.
2. Remaining market orders vs limit orders on the other side, by price-time priority.
3. Limit vs limit, by price-time priority.

On the side with more quantity than can trade, orders fill in this priority until the tradable quantity runs out:
**market orders (by time) → better-priced limit orders → earlier limit orders at the same price.**

Every pre-open trade happens at the equilibrium price, even for a buyer who bid higher or a seller who asked lower.

## 5. After the auction: unmatched orders

[S1] (NSE's page):
- All outstanding orders move to the normal market **keeping their original timestamp**.
- Unmatched **limit** orders keep their limit price.
- Unmatched **market** orders get the **equilibrium price** (they become limit orders at that price).
- If no equilibrium price was found, market orders move at the previous close (or adjusted close/base price),
  following price-time priority.

⚠️ **Conflict:** [S6] says unmatched market orders are converted to limit orders at the equilibrium price **with a
modified timestamp**. This matters for queue position at 09:15. Until an NSE circular settles it, make it a flag
(`keep_original_timestamp = true` by default, per NSE's own page).

## 6. Which securities take part

Equities, SME securities, partly paid-up securities, InvITs and REITs. [S5]
The `key=ALL` data had **2,445** securities on 2026-10-05.
IPOs and relisted securities use a separate *special* pre-open session (out of scope).

## 7. What NSE publishes, and what the free data really contains

During pre-open NSE shows the indicative equilibrium price, the indicative tradable quantity, total buy and sell
quantities, and the imbalance. [S1]

What `api/market-data-pre-open?key=ALL` returned on 2026-10-05 (checked against the saved file):

| Field | Meaning |
|---|---|
| `preopen[]` | Price levels: `price`, `buyQty`, `sellQty`; the IEP level has `iep: true` |
| `ato.totalBuyQuantity/totalSellQuantity`, `atoBuyQty`, `atoSellQty` | "At the open" = market order quantities |
| `IEP`, `finalPrice`, `finalQuantity`, `totalTradedVolume` | NSE's answer: price and traded quantity |
| `totalBuyQuantity`, `totalSellQuantity` | Totals across **all** levels, including ones not shown |
| `prevClose`, `lastUpdateTime` | For tie-break rule 3, and when the book last changed |

**Limits found:**
- Only about **10 price levels around the IEP** are shown. The full book is visible for only ~6% of stocks.
- **During order entry NSE shows the real pre-match book; after matching it shows the leftover book.**
  Checked 2026-10-06: buyers above / sellers below the IEP in **~88%** of stocks at 09:02–09:05, vs **10%** in the
  evening snapshot (and no stock with both sides left at the IEP). So use the **last snapshot before the random
  close** as the engine's input, and the post-09:12 snapshot as NSE's answer.
- The pre-match book is wider, so the 10-level window cuts more of it: only **~40 stocks** showed their full buy
  side at 09:05 (vs ~150 after matching). Exact checks will be a few hundred stock-days; most of the real-data
  proof will be consistency checks.
- The per-stock endpoint (`api/quote-equity?symbol=...`) returned **403** to a scripted request on 2026-10-06.
  Don't keep probing it during market hours: an IP block would also break the collector.

## 8. Open questions (resolve before or during Phase 1)

| # | Question | How to resolve |
|---|---|---|
| Q1 | Timestamp of unmatched orders moved to the normal market (§5) | Find the NSE circular for the 7 Sep 2026 change on NSE's circulars page |
| Q2 | Price band and tick size checks in pre-open (§2) | NSE FAQ on pre-open / price bands page |
| ~~Q3~~ | ~~Is the book shown during order entry pre-match or residual?~~ **Answered 2026-10-06: pre-match** (§7) | Done |
| Q4 | Why do 249 stocks in the 2026-10-05 snapshot still have buys above / sells below the IEP? | Explore the saved file (your puzzle) |
| Q5 | Market orders: do they count toward demand/supply at *every* candidate price? (Assumed yes, §3) | NSE FAQ / circular |
| Q6 | Modifying an order in pre-open: assumed only a quantity *reduction* keeps time priority (standard exchange practice; implemented in `AuctionBook::modify`) | NSE FAQ / circular |
| Q7 | Normal market: what happens to the unfilled part of a market order? Assumed **cancelled** (`OrderBook::submit`); some exchanges convert it to a limit order instead | NSE normal-market FAQ |

## 9. Worked example

Previous close ₹100. Orders (all limit):

| Buys | Sells |
|---|---|
| 300 @ 102 | 150 @ 99 |
| 200 @ 101 | 200 @ 100 |
| 100 @ 100 | 250 @ 101 |

| p | demand (buys ≥ p) | supply (sells ≤ p) | tradable | imbalance |
|---|---|---|---|---|
| 99 | 600 | 150 | 150 | 450 |
| 100 | 600 | 350 | 350 | 250 |
| **101** | 500 | 600 | **500** | 100 |
| 102 | 300 | 600 | 300 | 300 |

**Result:** equilibrium price **101**, 500 shares trade (rule 1 decides).
- Buys: 300@102 and 200@101 fill completely, both paying 101.
- Sells: 150@99 and 200@100 fill completely, then 150 of the 250@101 (price priority, then time).
- Moving to the normal market: buy 100@100 and sell 100@101, so the book opens at bid 100 / ask 101.

## 10. Test cases to build (your exercise)

Concrete order books for all nine: [test-cases.md](test-cases.md) (questions) and
[test-cases-answers.md](test-cases-answers.md) (answers, checked by brute force). Solve on paper before peeking.

Each one becomes a GoogleTest test in Phase 1. Work out the expected answer by hand **before** coding.
1. The worked example above.
2. Same example plus a **market buy of 50**: what changes?
3. A book where **rule 2** (min imbalance) breaks a tie in rule 1.
4. A book where **rule 3** (closest to previous close) decides.
5. The **halfway** case: the answer is the previous close itself.
6. **Only market orders** on both sides.
7. **No overlap:** best buy below best sell, no market orders, so no trades.
8. One side empty.
9. Allocation: the heavier side contains a market order and two limit orders at the same price. Who fills first?

---

## Sources
- [S1] NSE, *Pre-open session*: https://www.nseindia.com/static/products-services/equity-market-pre-open
- [S2] Business Standard, *Market pre-open rules change from today*: https://www.business-standard.com/markets/news/market-pre-open-rules-change-from-today-here-s-what-is-different-126090700065_1.html
- [S3] The Tribune, *NSE revises pre-open rules; market orders get priority over limit orders*: https://www.tribuneindia.com/news/circuit-filter/nse-revises-pre-open-rules-from-today-market-orders-get-priority-over-limit-orders
- [S4] 5paisa, *NSE revises pre-open session rules from September 7, 2026*: https://www.5paisa.com/blog/nse-pre-open-session-rules
- [S5] Kotak Neo, *NSE pre-open session rules change from 7 September 2026*: https://www.kotakneo.com/news/trading/nse-pre-open-session-new-order-rules-7-september/
- [S6] INDmoney, *NSE pre-open rules 2026: what changed?*: https://www.indmoney.com/blog/stocks/nse-pre-open-session-rules-changes
- [S7] NSE circular NSE/CMTR/75479 (30 Jul 2026): closing auction session live from 3 Aug 2026 (stretch goal context): https://nsearchives.nseindia.com/content/circulars/CMTR75479.pdf
- Data checks: `data/raw/2026-10-05/` (collected by `tools/collect/collect_preopen.py`).
