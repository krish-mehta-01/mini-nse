#include "mini_nse/order_book.h"

#include <algorithm>
#include <iterator>

namespace mini_nse {

namespace {

bool is_valid(const Order& order) {
    return order.quantity > 0 && (order.type == OrderType::Market || order.price > 0);
}

Trade make_trade(const Order& incoming, const Order& resting, Price price, Quantity quantity) {
    return incoming.side == Side::Buy ? Trade{incoming.id, resting.id, price, quantity}
                                      : Trade{resting.id, incoming.id, price, quantity};
}

}  // namespace

// `Levels` is the other side's price map: asks for an incoming buy, bids for an incoming sell.
// Both keep the best price at begin(), so the same loop serves both sides.
template <typename Levels>
void OrderBook::match(Order& incoming, Levels& opposite, std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !opposite.empty()) {
        if (incoming.type == OrderType::Limit && !crosses(incoming.side, incoming.price)) {
            break;  // the best price on the other side is worse than this order's limit
        }
        const auto best = opposite.begin();
        Queue& queue = best->second;
        Order& resting = queue.front();  // earliest order at the best price
        const Quantity quantity = std::min(incoming.quantity, resting.quantity);
        trades.push_back(make_trade(incoming, resting, best->first, quantity));  // at the resting price
        incoming.quantity -= quantity;
        resting.quantity -= quantity;
        if (resting.quantity == 0) {
            index_.erase(resting.id);
            queue.pop_front();
        }
        if (queue.empty()) {
            opposite.erase(best);
        }
    }
}

SubmitResult OrderBook::submit(Order order) {
    if (!is_valid(order) || index_.contains(order.id)) {
        return {};
    }
    order.sequence = next_sequence_++;
    SubmitResult result{.accepted = true, .trades = {}};
    if (order.side == Side::Buy) {
        match(order, asks_, result.trades);
    } else {
        match(order, bids_, result.trades);
    }
    if (order.quantity > 0 && order.type == OrderType::Limit) {
        Queue& queue = queue_at(order.side, order.price);
        queue.push_back(order);
        index_[order.id] = {order.side, order.price, std::prev(queue.end())};
    }
    return result;
}

bool OrderBook::restore(const Order& order) {
    if (!is_valid(order) || order.type != OrderType::Limit || index_.contains(order.id) ||
        crosses(order.side, order.price)) {
        return false;
    }
    Queue& queue = queue_at(order.side, order.price);
    // Keep the level in arrival order: go in front of the first order that arrived later.
    const auto later = std::ranges::find_if(queue, [&](const Order& o) { return o.sequence > order.sequence; });
    index_[order.id] = {order.side, order.price, queue.insert(later, order)};
    next_sequence_ = std::max(next_sequence_, order.sequence + 1);  // new orders queue behind restored ones
    return true;
}

bool OrderBook::cancel(OrderId id) {
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return false;
    }
    const Location location = found->second;
    queue_at(location.side, location.price).erase(location.position);
    index_.erase(found);
    erase_level_if_empty(location.side, location.price);
    return true;
}

SubmitResult OrderBook::modify(OrderId id, Price new_price, Quantity new_quantity) {
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return {};
    }
    Order& current = *found->second.position;
    Order updated = current;
    updated.price = new_price;
    updated.quantity = new_quantity;
    if (!is_valid(updated)) {
        return {};
    }
    if (new_price == current.price && new_quantity <= current.quantity) {
        current.quantity = new_quantity;  // asking for less keeps the place in the queue
        return {.accepted = true, .trades = {}};
    }
    cancel(id);
    return submit(updated);
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Quantity OrderBook::quantity_at(Side side, Price price) const {
    const auto total = [](const Queue& queue) {
        Quantity sum = 0;
        for (const Order& order : queue) {
            sum += order.quantity;
        }
        return sum;
    };
    if (side == Side::Buy) {
        const auto level = bids_.find(price);
        return level == bids_.end() ? 0 : total(level->second);
    }
    const auto level = asks_.find(price);
    return level == asks_.end() ? 0 : total(level->second);
}

std::vector<Order> OrderBook::orders(Side side) const {
    std::vector<Order> result;
    const auto append = [&result](const auto& levels) {
        for (const auto& [price, queue] : levels) {
            result.insert(result.end(), queue.begin(), queue.end());
        }
    };
    if (side == Side::Buy) {
        append(bids_);
    } else {
        append(asks_);
    }
    return result;
}

const Order* OrderBook::find(OrderId id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &*found->second.position;
}

OrderBook::Queue& OrderBook::queue_at(Side side, Price price) {
    return side == Side::Buy ? bids_[price] : asks_[price];
}

void OrderBook::erase_level_if_empty(Side side, Price price) {
    if (side == Side::Buy) {
        if (const auto level = bids_.find(price); level != bids_.end() && level->second.empty()) {
            bids_.erase(level);
        }
    } else if (const auto level = asks_.find(price); level != asks_.end() && level->second.empty()) {
        asks_.erase(level);
    }
}

bool OrderBook::crosses(Side side, Price price) const {
    if (side == Side::Buy) {
        const std::optional<Price> ask = best_ask();
        return ask && *ask <= price;
    }
    const std::optional<Price> bid = best_bid();
    return bid && *bid >= price;
}

}  // namespace mini_nse
