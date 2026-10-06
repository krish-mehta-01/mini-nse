#pragma once

#include <optional>
#include <variant>

#include "mini_nse/order.h"

namespace mini_nse {

// The stages of NSE's morning, in order. The engine never reads a clock: an input file says when each
// stage starts, so replaying the same file always gives the same result.
enum class Phase {
    Closed,      // before 9:00
    PreOpen,     // 9:00-9:05: limit and market orders
    LimitOnly,   // 9:05 until the random close: limit orders only; market orders are locked in
    Auction,     // order entry closed and the auction has run; buffer until 9:15
    Continuous,  // 9:15 onwards: the normal market
};

// What a morning is made of. One of these four per line of an input file.
struct AddOrder {
    Order order;
};

struct CancelOrder {
    OrderId id = 0;
};

struct ModifyOrder {
    OrderId id = 0;
    Price price = 0;  // ignored for market orders
    Quantity quantity = 0;
};

struct ChangePhase {
    Phase phase = Phase::Closed;
};

using Event = std::variant<AddOrder, CancelOrder, ModifyOrder, ChangePhase>;

// Per-stock rules that orders are checked against.
struct SessionConfig {
    Price previous_close = 0;
    Price tick_size = 1;              // prices must be a multiple of this; 1 paisa allows any price
    std::optional<Price> lower_band;  // limit prices outside [lower_band, upper_band] are rejected
    std::optional<Price> upper_band;
};

}  // namespace mini_nse
