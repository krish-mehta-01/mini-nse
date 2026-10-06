// mini_nse_bench: how long does each event take?
//
//   mini_nse_bench FILE [--csv OUT]
//
// Replays a morning twice. Pass 1 times every event on its own (std::chrono::steady_clock around
// Session::apply) and reports percentiles per kind of event: the typical case (p50) and the slow tail
// (p99, p99.9), which is what matters in trading. Pass 2 times the whole replay at once for throughput,
// without the per-event clock reads. Build with -DCMAKE_BUILD_TYPE=Release before trusting the numbers.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "mini_nse/script.h"
#include "mini_nse/session.h"

using namespace mini_nse;
using Clock = std::chrono::steady_clock;

namespace {

// Which kind of work an event is, judged by the phase it arrives in.
std::string kind_of(const Event& event, Phase phase) {
    const char* where = phase == Phase::Continuous ? "normal market" : "pre-open";
    if (std::holds_alternative<AddOrder>(event)) {
        return std::string(where) + ": new order";
    }
    if (std::holds_alternative<CancelOrder>(event)) {
        return std::string(where) + ": cancel";
    }
    if (std::holds_alternative<ModifyOrder>(event)) {
        return std::string(where) + ": modify";
    }
    const Phase next = std::get<ChangePhase>(event).phase;
    if (next == Phase::Auction) {
        return "auction (whole opening auction)";
    }
    return next == Phase::Continuous ? "9:15 hand-over" : "";
}

std::int64_t percentile(const std::vector<std::int64_t>& sorted, double p) {
    const auto k = static_cast<std::size_t>(p / 100.0 * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(k, sorted.size() - 1)];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mini_nse_bench FILE [--csv OUT]\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    const ParsedScript parsed = parse_script(in);
    if (!parsed.errors.empty()) {
        std::cerr << "line " << parsed.errors.front().line << ": " << parsed.errors.front().message << '\n';
        return 1;
    }
    const Script& script = parsed.script;

    // Pass 1: every event timed on its own.
    std::map<std::string, std::vector<std::int64_t>> nanos;
    std::size_t trades = 0;
    std::size_t rejects = 0;
    {
        Session session(script.config);
        for (const Event& event : script.events) {
            const Phase phase = session.phase();
            const auto start = Clock::now();
            const EventResult result = session.apply(event);
            const auto stop = Clock::now();
            trades += result.trades.size();
            rejects += result.reject != Reject::None;
            // Refused events do almost no work, so they get their own row instead of flattering the others.
            const std::string kind = result.reject != Reject::None ? "rejected (any kind)" : kind_of(event, phase);
            if (!kind.empty()) {
                nanos[kind].push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
            }
        }
    }

    // Pass 2: the whole morning in one go, for throughput.
    const auto start = Clock::now();
    {
        Session session(script.config);
        for (const Event& event : script.events) {
            trades += session.apply(event).trades.size();
        }
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();

    std::cout << script.events.size() << " events, " << trades / 2 << " trades, " << rejects << " rejects\n"
              << "throughput: " << std::fixed << std::setprecision(0)
              << static_cast<double>(script.events.size()) / seconds << " events/s (" << std::setprecision(3)
              << seconds * 1000 << " ms for the whole morning)\n\n"
              << std::left << std::setw(34) << "event" << std::right << std::setw(9) << "count" << std::setw(9)
              << "p50" << std::setw(9) << "p90" << std::setw(9) << "p99" << std::setw(9) << "p99.9" << std::setw(11)
              << "max" << "   (nanoseconds)\n";
    std::ofstream csv;
    if (argc >= 4 && std::string(argv[2]) == "--csv") {
        csv.open(argv[3]);
        csv << "kind,percentile,nanoseconds\n";
    }
    for (auto& [kind, values] : nanos) {
        std::ranges::sort(values);
        std::cout << std::left << std::setw(34) << kind << std::right << std::setw(9) << values.size();
        for (const double p : {50.0, 90.0, 99.0, 99.9}) {
            std::cout << std::setw(9) << percentile(values, p);
        }
        std::cout << std::setw(11) << values.back() << '\n';
        if (csv.is_open() && values.size() > 1) {
            for (const double p : {1.0, 5.0, 10.0, 25.0, 50.0, 75.0, 90.0, 95.0, 99.0, 99.5, 99.9, 99.99, 100.0}) {
                csv << kind << ',' << p << ',' << percentile(values, p) << '\n';
            }
        }
    }
    return 0;
}
