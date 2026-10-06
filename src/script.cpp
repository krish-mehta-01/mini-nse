#include "mini_nse/script.h"

#include <charconv>
#include <sstream>

#include "mini_nse/price.h"

namespace mini_nse {

namespace {

template <typename Int>
std::optional<Int> parse_int(const std::string& text) {
    Int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<Side> parse_side(const std::string& word) {
    if (word == "BUY") {
        return Side::Buy;
    }
    if (word == "SELL") {
        return Side::Sell;
    }
    return std::nullopt;
}

std::optional<Phase> parse_phase(const std::string& word) {
    if (word == "PREOPEN") {
        return Phase::PreOpen;
    }
    if (word == "LIMIT_ONLY") {
        return Phase::LimitOnly;
    }
    if (word == "AUCTION") {
        return Phase::Auction;
    }
    if (word == "CONTINUOUS") {
        return Phase::Continuous;
    }
    return std::nullopt;
}

// Reads one line's words into `script`. Returns an error message, or an empty string if the line was fine.
std::string parse_words(const std::vector<std::string>& words, Script& script) {
    const std::string& command = words[0];
    const std::size_t args = words.size() - 1;
    const bool settings_allowed = script.events.empty();

    if (command == "PREV_CLOSE" || command == "TICK" || command == "BAND") {
        if (!settings_allowed) {
            return command + " must come before the first event";
        }
        if (command == "BAND") {
            const auto low = args == 2 ? parse_price(words[1]) : std::nullopt;
            const auto high = args == 2 ? parse_price(words[2]) : std::nullopt;
            if (!low || !high || *low > *high) {
                return "expected BAND <low> <high>";
            }
            script.config.lower_band = low;
            script.config.upper_band = high;
            return {};
        }
        const auto price = args == 1 ? parse_price(words[1]) : std::nullopt;
        if (!price || *price <= 0) {
            return "expected " + command + " <price>";
        }
        (command == "TICK" ? script.config.tick_size : script.config.previous_close) = *price;
        return {};
    }

    if (command == "PHASE") {
        const auto phase = args == 1 ? parse_phase(words[1]) : std::nullopt;
        if (!phase) {
            return "expected PHASE PREOPEN|LIMIT_ONLY|AUCTION|CONTINUOUS";
        }
        script.events.push_back(ChangePhase{*phase});
        return {};
    }

    if (command == "ADD") {
        const auto id = args >= 1 ? parse_int<OrderId>(words[1]) : std::nullopt;
        const auto side = args >= 2 ? parse_side(words[2]) : std::nullopt;
        if (!id || !side || args < 3) {
            return "expected ADD <id> BUY|SELL LIMIT <price> <qty> or ADD <id> BUY|SELL MARKET <qty>";
        }
        Order order{.id = *id, .side = *side, .type = OrderType::Limit, .price = 0, .quantity = 0, .sequence = 0};
        if (words[3] == "LIMIT" && args == 5) {
            const auto price = parse_price(words[4]);
            const auto quantity = parse_int<Quantity>(words[5]);
            if (!price || !quantity) {
                return "bad price or quantity";
            }
            order.price = *price;
            order.quantity = *quantity;
        } else if (words[3] == "MARKET" && args == 4) {
            const auto quantity = parse_int<Quantity>(words[4]);
            if (!quantity) {
                return "bad quantity";
            }
            order.type = OrderType::Market;
            order.quantity = *quantity;
        } else {
            return "expected LIMIT <price> <qty> or MARKET <qty>";
        }
        script.events.push_back(AddOrder{order});
        return {};
    }

    if (command == "CANCEL") {
        const auto id = args == 1 ? parse_int<OrderId>(words[1]) : std::nullopt;
        if (!id) {
            return "expected CANCEL <id>";
        }
        script.events.push_back(CancelOrder{*id});
        return {};
    }

    if (command == "MODIFY") {
        const auto id = args == 3 ? parse_int<OrderId>(words[1]) : std::nullopt;
        const auto price = args == 3 ? parse_price(words[2]) : std::nullopt;
        const auto quantity = args == 3 ? parse_int<Quantity>(words[3]) : std::nullopt;
        if (!id || !price || !quantity) {
            return "expected MODIFY <id> <price> <qty>";
        }
        script.events.push_back(ModifyOrder{*id, *price, *quantity});
        return {};
    }

    return "unknown command " + command;
}

}  // namespace

ParsedScript parse_script(std::istream& in) {
    ParsedScript parsed;
    std::string line;
    int line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        line = line.substr(0, line.find('#'));
        std::istringstream words_in(line);
        std::vector<std::string> words;
        for (std::string word; words_in >> word;) {
            words.push_back(word);
        }
        if (words.empty()) {
            continue;
        }
        const std::size_t events_before = parsed.script.events.size();
        if (std::string error = parse_words(words, parsed.script); !error.empty()) {
            parsed.errors.push_back({line_number, std::move(error)});
        } else if (parsed.script.events.size() > events_before) {
            parsed.script.lines.push_back(line_number);
        }
    }
    if (parsed.script.config.previous_close == 0) {
        parsed.errors.push_back({0, "missing PREV_CLOSE"});
    }
    return parsed;
}

}  // namespace mini_nse
