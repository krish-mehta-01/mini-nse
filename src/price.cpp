#include "mini_nse/price.h"

#include <cstdio>

namespace mini_nse {

std::optional<Price> parse_price(std::string_view text) {
    const std::size_t dot = text.find('.');
    const std::string_view whole = text.substr(0, dot);
    const std::string_view fraction = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    if (whole.empty() || whole.size() > 12 || fraction.size() > 2 ||
        (dot != std::string_view::npos && fraction.empty())) {
        return std::nullopt;
    }
    Price paise = 0;
    for (const char digit : whole) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        paise = paise * 10 + (digit - '0');
    }
    for (std::size_t k = 0; k < 2; ++k) {  // "101.5" means 101.50
        const char digit = k < fraction.size() ? fraction[k] : '0';
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        paise = paise * 10 + (digit - '0');
    }
    return paise;
}

std::string format_price(Price price) {
    char text[32];
    const char* sign = price < 0 ? "-" : "";
    const Price magnitude = price < 0 ? -price : price;
    std::snprintf(text, sizeof text, "%s%lld.%02lld", sign, static_cast<long long>(magnitude / 100),
                  static_cast<long long>(magnitude % 100));
    return text;
}

}  // namespace mini_nse
