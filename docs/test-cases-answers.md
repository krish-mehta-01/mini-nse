# Pre-open auction: 9 test cases (answers)

Try [test-cases.md](test-cases.md) first. Equilibrium prices and quantities were double-checked by a brute-force
calculation over every candidate price.

"Trades" lists who traded with whom, in NSE's matching sequence: market vs market, then remaining market orders
vs limit orders, then limit vs limit, each by price-time priority. Every pre-open trade is at the opening price.

---

### Case 1: ₹101, 500 shares (rule 1)

| p | demand | supply | tradable | imbalance |
|---|---|---|---|---|
| 99 | 600 | 150 | 150 | 450 |
| 100 | 600 | 350 | 350 | 250 |
| **101** | 500 | 600 | **500** | 100 |
| 102 | 300 | 600 | 300 | 300 |

- **Filled:** B1 300, B2 200 (B3 at 100 is below 101, so not eligible). S1 150, S2 200, S3 150 of 250.
- **Trades @101:** B1–S1 150 · B1–S2 150 · B2–S2 50 · B2–S3 150
- **Moves to 9:15:** B3 buy 100 @100, S3 sell 100 @101, so the book opens at bid 100 / ask 101.
- **Lesson:** B1 bid 102 but pays 101; S1 asked 99 but gets 101.

### Case 2: ₹101, 550 shares (rule 1)

| p | demand | supply | tradable | imbalance |
|---|---|---|---|---|
| 99 | 650 | 150 | 150 | 500 |
| 100 | 650 | 350 | 350 | 300 |
| **101** | 550 | 600 | **550** | 50 |
| 102 | 350 | 600 | 350 | 250 |

- **Filled:** B4 50 (market, first), B1 300, B2 200. S1 150, S2 200, S3 200 of 250.
- **Trades @101:** B4–S1 50 · B1–S1 100 · B1–S2 200 · B2–S3 200
- **Moves to 9:15:** B3 buy 100 @100, S3 sell 50 @101.
- **Lesson:** a market order adds to demand at **every** price. The price stays the same and the quantity goes up by 50.

### Case 3: ₹100, 200 shares (rule 2)

| p | demand | supply | tradable | imbalance |
|---|---|---|---|---|
| **100** | 200 | 200 | **200** | **0** |
| 101 | 200 | 300 | 200 | 100 |

- Rule 1 ties (200 at both). **Rule 2 picks 100** (imbalance 0 vs 100). Rule 3 is never reached, so the
  previous close of 105 doesn't matter.
- **Filled:** B1 200, S1 200. **Trade @100:** B1–S1 200.
- **Moves to 9:15:** S2 sell 100 @101.

### Case 4: ₹101, 100 shares (rule 3)

| p | demand | supply | tradable | imbalance |
|---|---|---|---|---|
| 100 | 100 | 100 | 100 | 0 |
| **101** | 100 | 100 | 100 | 0 |

- Rules 1 and 2 both tie. **Rule 3:** 101 is closer to the previous close (103) than 100 is.
- **Trade @101:** B1–S1 100. Nothing moves to 9:15.

### Case 5: ₹100.50, 100 shares (halfway rule)
- Same table as case 4. The previous close (100.50) is **exactly halfway** between 100 and 101, so the opening
  price is **the previous close itself: ₹100.50**.
- **Trade @100.50:** B1–S1 100.
- **Lesson:** the opening price can be a price **nobody quoted**. Your code must handle that.

### Case 6: ₹100, 80 shares (only market orders)
- No limit prices, so there are no candidate prices. Only market orders on both sides → they match at the
  **previous close, ₹100**.
- **Trade @100:** B1–S1 80.
- **Moves to 9:15:** B1's remaining 20, as a buy at ₹100.

### Case 7: no price, no trades
| p | demand | supply | tradable |
|---|---|---|---|
| 99 | 100 | 0 | 0 |
| 101 | 0 | 100 | 0 |

- Tradable is 0 everywhere, so no price is discovered and nothing trades.
- **Moves to 9:15:** both orders, unchanged. The book opens at bid 99 / ask 101, and the day's open price is the
  first trade in the normal market.

### Case 8: no price, no trades (empty side)
- No sellers, so supply is 0 at every price. No price, no trades.
- **Moves to 9:15:** B1 100 @101 and B2 50 @100.
- **Lesson:** your code must not crash on an empty side.

### Case 9: ₹100, 120 shares, and the market order fills first
| p | demand | supply | tradable | imbalance |
|---|---|---|---|---|
| **100** | 250 | 120 | **120** | 130 |

- Buy side is heavier (250 vs 120), so buyers are rationed: **B3 (market) 50 first** even though it arrived last,
  then **B1 70** (earlier of the two at 100), **B2 0**.
- **Trades @100:** B3–S1 50 · B1–S1 70
- **Moves to 9:15:** B1 buy 30 @100 (keeps its original time, so it stays ahead of B2), B2 buy 100 @100.
- **Lesson:** since 7 Sep 2026, **market orders beat limit orders** regardless of arrival time.
