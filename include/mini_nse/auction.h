#pragma once

#include <optional>
#include <vector>

#include "mini_nse/auction_book.h"

namespace mini_nse {

// One row of the auction table: what would happen if the auction cleared at `price`.
struct PriceLevelStats {
    Price price = 0;
    Quantity demand = 0;     // market buys + limit buys priced at or above `price`
    Quantity supply = 0;     // market sells + limit sells priced at or below `price`
    Quantity tradable = 0;   // min(demand, supply): shares that could change hands
    Quantity imbalance = 0;  // |demand - supply|: shares left unmatched

    bool operator==(const PriceLevelStats&) const = default;
};

// Which of NSE's rules settled the price. Tests check it, so a right answer for the wrong
// reason still fails.
enum class DecidedBy {
    NoPrice,               // nothing can trade, so no equilibrium price
    MaxTradable,           // rule 1: most shares tradable
    MinImbalance,          // rule 2: least left unmatched
    NearestPreviousClose,  // rule 3: closest to the previous close
    HalfwayPreviousClose,  // rule 3: previous close exactly between the two nearest prices
    MarketOrdersOnly,      // no limit prices at all: market orders trade at the previous close
};

struct AuctionResult {
    std::optional<Price> price;  // empty when no price is discovered
    Quantity quantity = 0;       // shares that trade at `price`
    Quantity imbalance = 0;      // |demand - supply| at `price`
    DecidedBy decided_by = DecidedBy::NoPrice;
};

// Demand, supply, tradable and imbalance at every distinct limit price, lowest price first.
std::vector<PriceLevelStats> auction_table(const AuctionBook& book);

// NSE's equilibrium (opening) price: most tradable quantity, then least imbalance,
// then closest to the previous close. All prices in paise.
AuctionResult find_equilibrium(const AuctionBook& book, Price previous_close);

}  // namespace mini_nse
