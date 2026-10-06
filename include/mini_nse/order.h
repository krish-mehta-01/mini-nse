#pragma once

#include <cstdint>

namespace mini_nse {

// Prices are whole paise: Rs 100.50 is 10050. Integers keep every comparison exact,
// which the auction's "exactly halfway" rule depends on.
using Price = std::int64_t;

// Signed on purpose: demand - supply can be negative, and unsigned subtraction would wrap around.
using Quantity = std::int64_t;

using OrderId = std::uint64_t;

enum class Side { Buy, Sell };

// A limit order names the worst price it accepts; a market order accepts any price.
enum class OrderType { Limit, Market };

struct Order {
    OrderId id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    Price price = 0;              // ignored for market orders
    Quantity quantity = 0;
    std::uint64_t sequence = 0;   // arrival number, set by the book; lower = earlier = higher time priority
};

}  // namespace mini_nse
