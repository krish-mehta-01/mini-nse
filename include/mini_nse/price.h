#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mini_nse/order.h"

namespace mini_nse {

// "101.5", "101.50" or "101" -> 10150 paise, converted digit by digit so nothing is lost to floating point.
// Empty for anything else: a sign, more than two decimals, letters, or a number too large.
std::optional<Price> parse_price(std::string_view text);

// 10150 -> "101.50". Always two decimals, so output is easy to compare line by line.
std::string format_price(Price price);

}  // namespace mini_nse
