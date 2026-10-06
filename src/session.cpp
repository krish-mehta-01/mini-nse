#include "mini_nse/session.h"

#include <stdexcept>
#include <variant>

namespace mini_nse {

Session::Session(SessionConfig config) : config_(config) {}

EventResult Session::apply(const Event& event) {
    // std::visit calls the overload matching whichever of the four event kinds `event` holds.
    struct Dispatch {
        Session& session;
        EventResult operator()(const AddOrder& e) const { return session.add(e.order); }
        EventResult operator()(const CancelOrder& e) const { return session.cancel(e.id); }
        EventResult operator()(const ModifyOrder& e) const { return session.modify(e); }
        EventResult operator()(const ChangePhase& e) const { return session.change_phase(e.phase); }
    };
    return std::visit(Dispatch{*this}, event);
}

Reject Session::check_limit_price(Price price) const {
    if (price % config_.tick_size != 0) {
        return Reject::OffTick;
    }
    if ((config_.lower_band && price < *config_.lower_band) || (config_.upper_band && price > *config_.upper_band)) {
        return Reject::OutsideBand;
    }
    return Reject::None;
}

EventResult Session::add(const Order& order) {
    const bool pre_open = phase_ == Phase::PreOpen || phase_ == Phase::LimitOnly;
    if (!pre_open && phase_ != Phase::Continuous) {
        return {Reject::WrongPhase, {}};
    }
    if (order.quantity <= 0 || (order.type == OrderType::Limit && order.price <= 0)) {
        return {Reject::InvalidOrder, {}};
    }
    if (used_ids_.contains(order.id)) {
        return {Reject::DuplicateId, {}};
    }
    if (order.type == OrderType::Market && phase_ == Phase::LimitOnly) {
        return {Reject::MarketOrdersClosed, {}};
    }
    if (order.type == OrderType::Limit) {
        if (const Reject reject = check_limit_price(order.price); reject != Reject::None) {
            return {reject, {}};
        }
    }
    used_ids_.insert(order.id);
    if (pre_open) {
        auction_book_.add(order);
        return {};
    }
    return {Reject::None, order_book_.submit(order).trades};
}

EventResult Session::cancel(OrderId id) {
    if (phase_ == Phase::PreOpen || phase_ == Phase::LimitOnly) {
        const Order* order = auction_book_.find(id);
        if (order == nullptr) {
            return {Reject::UnknownOrder, {}};
        }
        if (order->type == OrderType::Market && phase_ == Phase::LimitOnly) {
            return {Reject::MarketOrdersClosed, {}};
        }
        auction_book_.cancel(id);
        return {};
    }
    if (phase_ == Phase::Continuous) {
        return {order_book_.cancel(id) ? Reject::None : Reject::UnknownOrder, {}};
    }
    return {Reject::WrongPhase, {}};
}

EventResult Session::modify(const ModifyOrder& change) {
    const bool pre_open = phase_ == Phase::PreOpen || phase_ == Phase::LimitOnly;
    if (!pre_open && phase_ != Phase::Continuous) {
        return {Reject::WrongPhase, {}};
    }
    const Order* order = pre_open ? auction_book_.find(change.id) : order_book_.find(change.id);
    if (order == nullptr) {
        return {Reject::UnknownOrder, {}};
    }
    const bool is_limit = order->type == OrderType::Limit;
    if (change.quantity <= 0 || (is_limit && change.price <= 0)) {
        return {Reject::InvalidOrder, {}};
    }
    if (!is_limit && phase_ == Phase::LimitOnly) {
        return {Reject::MarketOrdersClosed, {}};
    }
    if (is_limit) {
        if (const Reject reject = check_limit_price(change.price); reject != Reject::None) {
            return {reject, {}};
        }
    }
    if (pre_open) {
        auction_book_.modify(change.id, change.price, change.quantity);
        return {};
    }
    return {Reject::None, order_book_.modify(change.id, change.price, change.quantity).trades};
}

EventResult Session::change_phase(Phase next) {
    const bool allowed = (phase_ == Phase::Closed && next == Phase::PreOpen) ||
                         (phase_ == Phase::PreOpen && next == Phase::LimitOnly) ||
                         ((phase_ == Phase::PreOpen || phase_ == Phase::LimitOnly) && next == Phase::Auction) ||
                         (phase_ == Phase::Auction && next == Phase::Continuous);
    if (!allowed) {
        return {Reject::BadTransition, {}};
    }
    phase_ = next;
    if (next == Phase::Auction) {  // order entry just closed: run the auction
        auction_ = run_auction(auction_book_, config_.previous_close);
        return {Reject::None, auction_->trades};
    }
    if (next == Phase::Continuous) {  // 9:15: unfilled orders move to the normal market
        for (const Order& order : auction_->leftovers) {
            if (!order_book_.restore(order)) {
                throw std::logic_error("auction left an order that crosses the book");  // never expected
            }
        }
    }
    return {};
}

const char* to_string(Reject reject) {
    switch (reject) {
        case Reject::None: return "None";
        case Reject::WrongPhase: return "WrongPhase";
        case Reject::BadTransition: return "BadTransition";
        case Reject::InvalidOrder: return "InvalidOrder";
        case Reject::DuplicateId: return "DuplicateId";
        case Reject::UnknownOrder: return "UnknownOrder";
        case Reject::MarketOrdersClosed: return "MarketOrdersClosed";
        case Reject::OffTick: return "OffTick";
        case Reject::OutsideBand: return "OutsideBand";
    }
    return "?";
}

const char* to_string(Phase phase) {
    switch (phase) {
        case Phase::Closed: return "CLOSED";
        case Phase::PreOpen: return "PREOPEN";
        case Phase::LimitOnly: return "LIMIT_ONLY";
        case Phase::Auction: return "AUCTION";
        case Phase::Continuous: return "CONTINUOUS";
    }
    return "?";
}

const char* to_string(DecidedBy rule) {
    switch (rule) {
        case DecidedBy::NoPrice: return "NoPrice";
        case DecidedBy::MaxTradable: return "MaxTradable";
        case DecidedBy::MinImbalance: return "MinImbalance";
        case DecidedBy::NearestPreviousClose: return "NearestPreviousClose";
        case DecidedBy::HalfwayPreviousClose: return "HalfwayPreviousClose";
        case DecidedBy::MarketOrdersOnly: return "MarketOrdersOnly";
    }
    return "?";
}

}  // namespace mini_nse
