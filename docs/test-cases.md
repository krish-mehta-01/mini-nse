# Pre-open auction: 9 test cases (questions)

Solve each one **on paper first**, then check against [test-cases-answers.md](test-cases-answers.md).
These become the GoogleTest tests in Phase 1 (price + quantity) and Phase 2 (who gets filled).

**For every case, work out:**
1. The table: for each candidate price, demand (buys willing), supply (sells willing), tradable = min, imbalance = |demand − supply|.
2. The **opening price** and **quantity traded** (rules: most tradable → least imbalance → closest to previous close).
3. **Who gets filled, and how much** (market orders first → better price → earlier time).
4. **What moves to the 9:15 normal market.**

"Arrived" is the order of arrival (1 = first). A **market** order has no price: it accepts any price.
Rules: [nse-rules.md](nse-rules.md) §3–§5.

---

### Case 1: the basic example
Previous close: ₹100

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B1 | 1 | Buy | Limit | 102 | 300 |
| S1 | 2 | Sell | Limit | 99 | 150 |
| B2 | 3 | Buy | Limit | 101 | 200 |
| S2 | 4 | Sell | Limit | 100 | 200 |
| B3 | 5 | Buy | Limit | 100 | 100 |
| S3 | 6 | Sell | Limit | 101 | 250 |

### Case 2: add a market order
Same six orders as case 1, plus:

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B4 | 7 | Buy | **Market** | — | 50 |

Does the price change? Does the quantity change? Who fills first among the buyers?

### Case 3: rule 2 decides
Previous close: ₹105

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| S1 | 1 | Sell | Limit | 100 | 200 |
| S2 | 2 | Sell | Limit | 101 | 100 |
| B1 | 3 | Buy | Limit | 101 | 200 |

Careful: the previous close is closer to 101. Does that matter here?

### Case 4: rule 3 decides
Previous close: ₹103

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| S1 | 1 | Sell | Limit | 100 | 100 |
| B1 | 2 | Buy | Limit | 101 | 100 |

### Case 5: the halfway case
Same two orders as case 4, but previous close: **₹100.50**

### Case 6: only market orders
Previous close: ₹100

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B1 | 1 | Buy | Market | — | 100 |
| S1 | 2 | Sell | Market | — | 80 |

### Case 7: no overlap
Previous close: ₹100

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B1 | 1 | Buy | Limit | 99 | 100 |
| S1 | 2 | Sell | Limit | 101 | 100 |

### Case 8: one side empty
Previous close: ₹100

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B1 | 1 | Buy | Limit | 101 | 100 |
| B2 | 2 | Buy | Limit | 100 | 50 |

### Case 9: who fills first?
Previous close: ₹100

| Order | Arrived | Side | Type | Price | Qty |
|---|---|---|---|---|---|
| B1 | 1 | Buy | Limit | 100 | 100 |
| B2 | 2 | Buy | Limit | 100 | 100 |
| S1 | 3 | Sell | Limit | 100 | 120 |
| B3 | 4 | Buy | **Market** | — | 50 |

B3 arrived **last**. Where does it stand in the queue?
