#pragma once

#include "mini_nse/order.h"

namespace mini_nse {

// `quantity` shares change hands at `price` between one buy order and one sell order.
struct Trade {
    OrderId buy_id = 0;
    OrderId sell_id = 0;
    Price price = 0;
    Quantity quantity = 0;

    bool operator==(const Trade&) const = default;
};

}  // namespace mini_nse
