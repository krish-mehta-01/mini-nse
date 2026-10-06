#pragma once

#include <initializer_list>
#include <ostream>

#include <gtest/gtest.h>

#include "mini_nse/auction.h"
#include "mini_nse/auction_book.h"

namespace mini_nse {

// Rupees and paise to Price: rs(100, 50) is Rs 100.50 = 10050 paise.
constexpr Price rs(std::int64_t rupees, std::int64_t paise = 0) {
    return rupees * 100 + paise;
}

inline Order limit_order(OrderId id, Side side, Price price, Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Limit, .price = price, .quantity = quantity, .sequence = 0};
}

inline Order market_order(OrderId id, Side side, Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Market, .price = 0, .quantity = quantity, .sequence = 0};
}

// Adds the orders in the order given (their arrival order). Every add must succeed.
inline AuctionBook book_of(std::initializer_list<Order> orders) {
    AuctionBook book;
    for (const Order& order : orders) {
        EXPECT_TRUE(book.add(order)) << "could not add order " << order.id;
    }
    return book;
}

// Readable failure messages: GoogleTest finds these through the type's namespace.
inline void PrintTo(const PriceLevelStats& row, std::ostream* out) {
    *out << "{price " << row.price << ", demand " << row.demand << ", supply " << row.supply
         << ", tradable " << row.tradable << ", imbalance " << row.imbalance << "}";
}

}  // namespace mini_nse
