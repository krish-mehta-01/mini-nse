#pragma once

#include <optional>
#include <unordered_set>
#include <vector>

#include "mini_nse/auction.h"
#include "mini_nse/auction_book.h"
#include "mini_nse/events.h"
#include "mini_nse/order_book.h"

namespace mini_nse {

// Why an event was refused. None means it was accepted.
enum class Reject {
    None,
    WrongPhase,          // e.g. an order during the 9:12-9:15 buffer
    BadTransition,       // phases must go PreOpen -> LimitOnly -> Auction -> Continuous
    InvalidOrder,        // quantity <= 0, or a limit order without a price
    DuplicateId,         // ids are unique for the whole day
    UnknownOrder,        // cancel or modify of an order that isn't waiting
    MarketOrdersClosed,  // after 9:05: no new market orders, and existing ones are locked in
    OffTick,             // price isn't a multiple of the tick size
    OutsideBand,         // price outside the allowed band
};

struct EventResult {
    Reject reject = Reject::None;
    std::vector<Trade> trades;  // trades this event caused, in order

    bool operator==(const EventResult&) const = default;
};

// One stock's trading morning. Feed it events in order; it routes each one to the auction book (pre-open)
// or the order book (normal market), checks it against the rules of the current phase, runs the auction when
// order entry closes, and hands the leftovers to the normal market at 9:15.
class Session {
public:
    explicit Session(SessionConfig config);

    EventResult apply(const Event& event);

    Phase phase() const { return phase_; }
    const SessionConfig& config() const { return config_; }
    const std::optional<AuctionOutcome>& auction() const { return auction_; }  // set once the auction has run
    const AuctionBook& auction_book() const { return auction_book_; }
    const OrderBook& order_book() const { return order_book_; }

private:
    EventResult add(const Order& order);
    EventResult cancel(OrderId id);
    EventResult modify(const ModifyOrder& change);
    EventResult change_phase(Phase next);
    Reject check_limit_price(Price price) const;  // tick size and band

    SessionConfig config_;
    Phase phase_ = Phase::Closed;
    AuctionBook auction_book_;
    OrderBook order_book_;
    std::optional<AuctionOutcome> auction_;
    std::unordered_set<OrderId> used_ids_;
};

const char* to_string(Reject reject);
const char* to_string(Phase phase);
const char* to_string(DecidedBy rule);

}  // namespace mini_nse
