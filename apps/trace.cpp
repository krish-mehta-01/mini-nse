// mini_nse_trace: plays a morning file and writes every step as JSON, for the web viewer (viewer/).
//
//   mini_nse_trace FILE > trace.json
//
// Each step records the event, whether it was accepted, the trades it caused, and the book afterwards. The step
// that closes order entry also records the whole auction table: demand, supply, tradable and imbalance at every
// price, and which of NSE's rules removed each price. Prices are in paise; the viewer formats them.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "mini_nse/auction.h"
#include "mini_nse/price.h"
#include "mini_nse/script.h"
#include "mini_nse/session.h"

using namespace mini_nse;

namespace {

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
        }
        out += ch;
    }
    return out + '"';
}

const char* side_name(Side side) { return side == Side::Buy ? "BUY" : "SELL"; }

// The event as the line that would produce it, e.g. "ADD 1 BUY LIMIT 102.00 300".
std::string describe(const Event& event) {
    if (const auto* add = std::get_if<AddOrder>(&event)) {
        const Order& o = add->order;
        std::string text = "ADD " + std::to_string(o.id) + ' ' + side_name(o.side) + ' ';
        text += o.type == OrderType::Market ? "MARKET" : "LIMIT " + format_price(o.price);
        return text + ' ' + std::to_string(o.quantity);
    }
    if (const auto* cancel = std::get_if<CancelOrder>(&event)) {
        return "CANCEL " + std::to_string(cancel->id);
    }
    if (const auto* modify = std::get_if<ModifyOrder>(&event)) {
        return "MODIFY " + std::to_string(modify->id) + ' ' + format_price(modify->price) + ' ' +
               std::to_string(modify->quantity);
    }
    return std::string("PHASE ") + to_string(std::get<ChangePhase>(event).phase);
}

std::string order_json(const Order& o) {
    return "{\"id\":" + std::to_string(o.id) + ",\"side\":\"" + side_name(o.side) + "\",\"type\":\"" +
           (o.type == OrderType::Market ? "MARKET" : "LIMIT") + "\",\"price\":" + std::to_string(o.price) +
           ",\"qty\":" + std::to_string(o.quantity) + ",\"seq\":" + std::to_string(o.sequence) + '}';
}

std::string orders_json(const std::vector<Order>& orders) {
    std::string out = "[";
    for (std::size_t k = 0; k < orders.size(); ++k) {
        out += (k ? "," : "") + order_json(orders[k]);
    }
    return out + ']';
}

std::string trades_json(const std::vector<Trade>& trades) {
    std::string out = "[";
    for (std::size_t k = 0; k < trades.size(); ++k) {
        const Trade& t = trades[k];
        out += (k ? ",{" : "{") + std::string("\"buy\":") + std::to_string(t.buy_id) + ",\"sell\":" +
               std::to_string(t.sell_id) + ",\"price\":" + std::to_string(t.price) + ",\"qty\":" +
               std::to_string(t.quantity) + '}';
    }
    return out + ']';
}

// What is waiting after this step, in the order the viewer should show it.
std::string book_json(const Session& session) {
    std::vector<Order> orders;
    std::string kind;
    if (session.phase() == Phase::Continuous) {
        kind = "normal market";
        orders = session.order_book().orders(Side::Buy);
        const std::vector<Order> sells = session.order_book().orders(Side::Sell);
        orders.insert(orders.end(), sells.begin(), sells.end());
    } else if (session.phase() == Phase::Auction) {
        kind = "auction leftovers";
        orders = session.auction()->leftovers;
    } else {
        kind = "pre-open";
        orders = session.auction_book().orders();
    }
    return "{\"kind\":\"" + kind + "\",\"orders\":" + orders_json(orders) + '}';
}

// The auction table with the rule that removed each price (0 = it survived every rule). The price and the rule
// that decided it come from the engine; this only reproduces the funnel so the viewer can show each step.
std::string auction_json(const AuctionBook& book, const AuctionResult& result, Price previous_close) {
    const std::vector<PriceLevelStats> rows = auction_table(book);
    std::vector<int> out_at(rows.size(), 0);
    std::vector<std::size_t> alive;
    for (std::size_t k = 0; k < rows.size(); ++k) {
        alive.push_back(k);
    }
    const auto keep = [&](int rule, auto&& value, bool largest) {
        if (alive.size() <= 1) {
            return;
        }
        auto best = value(rows[alive.front()]);
        for (const std::size_t k : alive) {
            best = largest ? std::max(best, value(rows[k])) : std::min(best, value(rows[k]));
        }
        std::vector<std::size_t> next;
        for (const std::size_t k : alive) {
            if (value(rows[k]) == best) {
                next.push_back(k);
            } else {
                out_at[k] = rule;
            }
        }
        alive = next;
    };
    if (result.price) {
        keep(1, [](const PriceLevelStats& r) { return r.tradable; }, true);
        keep(2, [](const PriceLevelStats& r) { return r.imbalance; }, false);
        keep(3, [&](const PriceLevelStats& r) { return std::abs(r.price - previous_close); }, false);
    } else {
        std::fill(out_at.begin(), out_at.end(), 1);  // nothing can trade at any price
    }

    Quantity market_buy = 0, market_sell = 0;
    for (const Order& o : book.orders()) {
        if (o.type == OrderType::Market) {
            (o.side == Side::Buy ? market_buy : market_sell) += o.quantity;
        }
    }
    std::string out = "{\"price\":" + (result.price ? std::to_string(*result.price) : std::string("null")) +
                      ",\"quantity\":" + std::to_string(result.quantity) + ",\"imbalance\":" +
                      std::to_string(result.imbalance) + ",\"rule\":\"" + to_string(result.decided_by) +
                      "\",\"market_buy\":" + std::to_string(market_buy) + ",\"market_sell\":" +
                      std::to_string(market_sell) + ",\"table\":[";
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const PriceLevelStats& r = rows[k];
        out += (k ? ",{" : "{") + std::string("\"price\":") + std::to_string(r.price) + ",\"demand\":" +
               std::to_string(r.demand) + ",\"supply\":" + std::to_string(r.supply) + ",\"tradable\":" +
               std::to_string(r.tradable) + ",\"imbalance\":" + std::to_string(r.imbalance) + ",\"out_at\":" +
               std::to_string(out_at[k]) + '}';
    }
    return out + "]}";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mini_nse_trace FILE > trace.json\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 1;
    }
    const ParsedScript parsed = parse_script(in);
    if (!parsed.errors.empty()) {
        for (const ParseError& error : parsed.errors) {
            std::cerr << "line " << error.line << ": " << error.message << '\n';
        }
        return 1;
    }

    const Script& script = parsed.script;
    const SessionConfig& config = script.config;
    Session session(config);
    std::cout << "{\"previous_close\":" << config.previous_close << ",\"tick\":" << config.tick_size
              << ",\"band\":"
              << (config.lower_band ? "[" + std::to_string(*config.lower_band) + "," +
                                          std::to_string(*config.upper_band) + "]"
                                    : std::string("null"))
              << ",\"steps\":[";
    for (std::size_t k = 0; k < script.events.size(); ++k) {
        const Event& event = script.events[k];
        const Phase before = session.phase();
        const EventResult result = session.apply(event);
        const auto* change = std::get_if<ChangePhase>(&event);
        const bool auction_ran = result.reject == Reject::None && change && change->phase == Phase::Auction;
        std::cout << (k ? ",\n" : "\n") << "{\"line\":" << script.lines[k] << ",\"text\":" << quoted(describe(event))
                  << ",\"phase_before\":\"" << to_string(before) << "\",\"phase\":\"" << to_string(session.phase())
                  << "\",\"reject\":"
                  << (result.reject == Reject::None ? std::string("null") : quoted(to_string(result.reject)))
                  << ",\"stage\":\"" << (auction_ran ? "auction" : "normal") << "\",\"trades\":"
                  << trades_json(result.trades) << ",\"book\":" << book_json(session) << ",\"auction\":"
                  << (auction_ran ? auction_json(session.auction_book(), session.auction()->result,
                                                 config.previous_close)
                                  : std::string("null"))
                  << '}';
    }
    std::cout << "\n]}\n";
    return 0;
}
