#pragma once

#include <unordered_set>
#include <vector>

#include "mini_nse/order.h"

namespace mini_nse {

// The orders collected during pre-open order entry. Nothing trades here: the auction runs later,
// on whatever the book holds when order entry closes. Orders are kept earliest first.
class AuctionBook {
public:
    // Returns false and changes nothing for a duplicate id, a quantity <= 0,
    // or a limit order with a price <= 0. Assigns the order its arrival sequence.
    bool add(Order order);

    // Returns false if no order has this id.
    bool cancel(OrderId id);

    // Sets a new price (limit orders only; ignored for market orders) and quantity.
    // Only reducing the quantity keeps time priority; any other change sends the order to the back.
    // Returns false for an unknown id or invalid values.
    bool modify(OrderId id, Price new_price, Quantity new_quantity);

    const std::vector<Order>& orders() const { return orders_; }

    // nullptr if no order has this id.
    const Order* find(OrderId id) const;

private:
    std::vector<Order> orders_;  // a plain vector: simple and fast enough here; Phase 5 measures alternatives
    std::unordered_set<OrderId> ids_;  // the ids in orders_: makes the duplicate check in add() O(1), not a scan
    std::uint64_t next_sequence_ = 1;
};

}  // namespace mini_nse
