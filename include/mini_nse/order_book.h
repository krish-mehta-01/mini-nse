#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "mini_nse/order.h"
#include "mini_nse/trade.h"

namespace mini_nse {

struct SubmitResult {
    bool accepted = false;      // false: rejected, nothing changed
    std::vector<Trade> trades;  // in the order they happened
};

// The normal market from 9:15: a price-time priority limit order book. Every incoming order trades
// immediately against the best prices on the other side, at those resting orders' prices. What's left
// of a limit order waits in the book; what's left of a market order is cancelled.
class OrderBook {
public:
    // Rejected for a duplicate id, a quantity <= 0, or a limit order with a price <= 0.
    // The order gets the next arrival sequence, behind everything already in the book.
    SubmitResult submit(Order order);

    // Puts an order carried over from the auction back in, keeping its original arrival sequence, without
    // matching. Rejected if invalid, a duplicate, a market order, or if it would cross the book
    // (a correct auction never leaves crossing orders, so that signals a bug).
    bool restore(const Order& order);

    bool cancel(OrderId id);

    // Reducing quantity at the same price keeps time priority. Any other change is a cancel plus a new
    // submit: the order goes to the back of the queue and may trade straight away.
    SubmitResult modify(OrderId id, Price new_price, Quantity new_quantity);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity quantity_at(Side side, Price price) const;  // total waiting at that price
    std::vector<Order> orders(Side side) const;          // best priority first
    const Order* find(OrderId id) const;                 // nullptr if not in the book
    std::size_t size() const { return index_.size(); }

private:
    // One price level: orders earliest first. A list, because erasing one order leaves the iterators
    // pointing at all the others valid, which is what lets index_ find any order instantly.
    using Queue = std::list<Order>;

    struct Location {
        Side side = Side::Buy;
        Price price = 0;
        Queue::iterator position;
    };

    Queue& queue_at(Side side, Price price);  // creates the level if it doesn't exist yet
    void erase_level_if_empty(Side side, Price price);
    bool crosses(Side side, Price price) const;  // would a limit order at `price` trade right away?

    template <typename Levels>
    void match(Order& incoming, Levels& opposite, std::vector<Trade>& trades);

    std::map<Price, Queue, std::greater<>> bids_;  // highest price first
    std::map<Price, Queue> asks_;                  // lowest price first
    std::unordered_map<OrderId, Location> index_;  // order id -> exactly where it waits
    std::uint64_t next_sequence_ = 1;
};

}  // namespace mini_nse
