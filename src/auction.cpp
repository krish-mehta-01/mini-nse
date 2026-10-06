#include "mini_nse/auction.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace mini_nse {

namespace {

struct LevelQuantity {
    Quantity buy = 0;
    Quantity sell = 0;
};

// The book summed up once: limit quantity per price, plus market orders, which count at every price.
struct BookTotals {
    std::map<Price, LevelQuantity> levels;  // std::map keeps prices sorted, lowest first
    Quantity market_buy = 0;
    Quantity market_sell = 0;
    Quantity limit_buy = 0;
};

BookTotals sum_book(const AuctionBook& book) {
    BookTotals totals;
    for (const Order& order : book.orders()) {
        const bool is_buy = order.side == Side::Buy;
        if (order.type == OrderType::Market) {
            (is_buy ? totals.market_buy : totals.market_sell) += order.quantity;
        } else if (is_buy) {
            totals.levels[order.price].buy += order.quantity;
            totals.limit_buy += order.quantity;
        } else {
            totals.levels[order.price].sell += order.quantity;
        }
    }
    return totals;
}

PriceLevelStats make_row(Price price, Quantity demand, Quantity supply) {
    return {price, demand, supply, std::min(demand, supply), std::abs(demand - supply)};
}

// One pass from the lowest price up. Moving up a price, more sellers become willing (supply grows)
// and the cheapest buyers drop out (demand shrinks), so running sums replace re-adding the whole book
// at every price: O(levels) instead of O(levels x orders).
std::vector<PriceLevelStats> table_from(const BookTotals& totals) {
    std::vector<PriceLevelStats> rows;
    rows.reserve(totals.levels.size());
    Quantity buys_below = 0;         // limit buys priced below the current price
    Quantity sells_at_or_below = 0;  // limit sells priced at or below the current price
    for (const auto& [price, quantity] : totals.levels) {
        sells_at_or_below += quantity.sell;
        rows.push_back(make_row(price,
                                totals.market_buy + totals.limit_buy - buys_below,
                                totals.market_sell + sells_at_or_below));
        buys_below += quantity.buy;
    }
    return rows;
}

// Demand and supply at any price, including one where nobody placed an order (the halfway case).
PriceLevelStats stats_at(const BookTotals& totals, Price price) {
    Quantity demand = totals.market_buy;
    Quantity supply = totals.market_sell;
    for (const auto& [level_price, quantity] : totals.levels) {
        if (level_price >= price) {
            demand += quantity.buy;
        }
        if (level_price <= price) {
            supply += quantity.sell;
        }
    }
    return make_row(price, demand, supply);
}

AuctionResult result_from(const PriceLevelStats& row, DecidedBy rule) {
    return {row.price, row.tradable, row.imbalance, rule};
}

}  // namespace

std::vector<PriceLevelStats> auction_table(const AuctionBook& book) {
    return table_from(sum_book(book));
}

AuctionResult find_equilibrium(const AuctionBook& book, Price previous_close) {
    const BookTotals totals = sum_book(book);
    std::vector<PriceLevelStats> candidates = table_from(totals);

    if (candidates.empty()) {  // no limit orders, so no candidate prices
        if (totals.market_buy > 0 && totals.market_sell > 0) {
            const PriceLevelStats row = make_row(previous_close, totals.market_buy, totals.market_sell);
            return result_from(row, DecidedBy::MarketOrdersOnly);
        }
        return {};
    }

    // Rule 1: the price where the most shares can trade.
    const Quantity most = std::ranges::max(candidates, {}, &PriceLevelStats::tradable).tradable;
    if (most == 0) {
        return {};  // e.g. every buyer bids below every seller
    }
    std::erase_if(candidates, [most](const PriceLevelStats& row) { return row.tradable != most; });
    if (candidates.size() == 1) {
        return result_from(candidates.front(), DecidedBy::MaxTradable);
    }

    // Rule 2: among those, the price that leaves the fewest shares unmatched.
    const Quantity least = std::ranges::min(candidates, {}, &PriceLevelStats::imbalance).imbalance;
    std::erase_if(candidates, [least](const PriceLevelStats& row) { return row.imbalance != least; });
    if (candidates.size() == 1) {
        return result_from(candidates.front(), DecidedBy::MinImbalance);
    }

    // Rule 3: the price closest to yesterday's close. If two are equally close, they sit on either
    // side of it, and the previous close itself becomes the price, even though nobody quoted it.
    const auto distance = [previous_close](const PriceLevelStats& row) {
        return std::abs(row.price - previous_close);
    };
    const Price nearest = distance(std::ranges::min(candidates, {}, distance));
    std::erase_if(candidates, [&](const PriceLevelStats& row) { return distance(row) != nearest; });
    if (candidates.size() == 1) {
        return result_from(candidates.front(), DecidedBy::NearestPreviousClose);
    }
    return result_from(stats_at(totals, previous_close), DecidedBy::HalfwayPreviousClose);
}

}  // namespace mini_nse
