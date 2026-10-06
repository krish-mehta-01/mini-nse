#include "mini_nse/auction_book.h"

#include <algorithm>

namespace mini_nse {

namespace {

bool is_valid(OrderType type, Price price, Quantity quantity) {
    return quantity > 0 && (type == OrderType::Market || price > 0);
}

}  // namespace

bool AuctionBook::add(Order order) {
    if (!is_valid(order.type, order.price, order.quantity) || find(order.id) != nullptr) {
        return false;
    }
    order.sequence = next_sequence_++;
    orders_.push_back(order);
    return true;
}

bool AuctionBook::cancel(OrderId id) {
    const auto it = std::ranges::find(orders_, id, &Order::id);
    if (it == orders_.end()) {
        return false;
    }
    orders_.erase(it);
    return true;
}

bool AuctionBook::modify(OrderId id, Price new_price, Quantity new_quantity) {
    const auto it = std::ranges::find(orders_, id, &Order::id);
    if (it == orders_.end() || !is_valid(it->type, new_price, new_quantity)) {
        return false;
    }

    Order updated = *it;
    if (updated.type == OrderType::Limit) {
        updated.price = new_price;
    }
    updated.quantity = new_quantity;

    // Asking for less at the same price can't hurt anyone behind you, so the order keeps its place.
    const bool keeps_priority = updated.price == it->price && updated.quantity <= it->quantity;
    if (keeps_priority) {
        *it = updated;
        return true;
    }
    orders_.erase(it);
    updated.sequence = next_sequence_++;
    orders_.push_back(updated);
    return true;
}

const Order* AuctionBook::find(OrderId id) const {
    const auto it = std::ranges::find(orders_, id, &Order::id);
    return it == orders_.end() ? nullptr : &*it;
}

}  // namespace mini_nse
