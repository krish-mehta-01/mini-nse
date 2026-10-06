// mini_nse_replay: plays morning files through the engine and prints what happened, one fact per line.
//
//   mini_nse_replay FILE...
//
// For each file:
//   === <file>
//   REJECT <line> <reason>                       an event the session refused
//   AUCTION <price|none> <qty> <imbalance> <rule> when order entry closes
//   TRADE A|C <buy id> <sell id> <price> <qty>   A = auction, C = continuous (normal market)
//   BOOK BUY|SELL <id> LIMIT <price>|MARKET <qty> what is still waiting at the end, best priority first
//
// The format is deliberately plain so tools/reference.py can produce the very same lines.
#include <fstream>
#include <iostream>
#include <string>
#include <variant>

#include "mini_nse/price.h"
#include "mini_nse/script.h"
#include "mini_nse/session.h"

using namespace mini_nse;

namespace {

const char* side_name(Side side) { return side == Side::Buy ? "BUY" : "SELL"; }

void print_trades(std::ostream& out, char stage, const std::vector<Trade>& trades) {
    for (const Trade& trade : trades) {
        out << "TRADE " << stage << ' ' << trade.buy_id << ' ' << trade.sell_id << ' ' << format_price(trade.price)
            << ' ' << trade.quantity << '\n';
    }
}

void print_waiting(std::ostream& out, const Order& order) {
    out << "BOOK " << side_name(order.side) << ' ' << order.id << ' ';
    if (order.type == OrderType::Market) {
        out << "MARKET";
    } else {
        out << "LIMIT " << format_price(order.price);
    }
    out << ' ' << order.quantity << '\n';
}

int replay(const std::string& path, std::ostream& out) {
    std::ifstream in(path);
    if (!in) {
        out << "ERROR 0 cannot open file\n";
        return 1;
    }
    const ParsedScript parsed = parse_script(in);
    if (!parsed.errors.empty()) {
        for (const ParseError& error : parsed.errors) {
            out << "ERROR " << error.line << ' ' << error.message << '\n';
        }
        return 1;
    }

    const Script& script = parsed.script;
    Session session(script.config);
    for (std::size_t k = 0; k < script.events.size(); ++k) {
        const Event& event = script.events[k];
        const EventResult result = session.apply(event);
        if (result.reject != Reject::None) {
            out << "REJECT " << script.lines[k] << ' ' << to_string(result.reject) << '\n';
            continue;
        }
        const auto* change = std::get_if<ChangePhase>(&event);
        if (change != nullptr && change->phase == Phase::Auction) {
            const AuctionResult& auction = session.auction()->result;
            out << "AUCTION " << (auction.price ? format_price(*auction.price) : "none") << ' ' << auction.quantity
                << ' ' << auction.imbalance << ' ' << to_string(auction.decided_by) << '\n';
            print_trades(out, 'A', result.trades);
        } else {
            print_trades(out, 'C', result.trades);
        }
    }

    // Whatever is still waiting: the normal market's book, the auction's leftovers, or the pre-open orders.
    if (session.phase() == Phase::Continuous) {
        for (const Side side : {Side::Buy, Side::Sell}) {
            for (const Order& order : session.order_book().orders(side)) {
                print_waiting(out, order);
            }
        }
    } else if (session.phase() == Phase::Auction) {
        for (const Order& order : session.auction()->leftovers) {
            print_waiting(out, order);
        }
    } else {
        for (const Order& order : session.auction_book().orders()) {
            print_waiting(out, order);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mini_nse_replay FILE...\n";
        return 2;
    }
    std::ios::sync_with_stdio(false);  // faster output when replaying thousands of files
    int status = 0;
    for (int k = 1; k < argc; ++k) {
        std::cout << "=== " << argv[k] << '\n';
        status |= replay(argv[k], std::cout);
    }
    return status;
}
