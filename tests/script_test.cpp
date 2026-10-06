// Phase 3: reading a morning from a text file.
#include <sstream>

#include "mini_nse/price.h"
#include "mini_nse/script.h"
#include "test_helpers.h"

namespace mini_nse {
namespace {

TEST(Price, ParsesRupeesExactly) {
    EXPECT_EQ(parse_price("101.50"), 10150);
    EXPECT_EQ(parse_price("101.5"), 10150);
    EXPECT_EQ(parse_price("101"), 10100);
    EXPECT_EQ(parse_price("0.05"), 5);
    EXPECT_EQ(parse_price("1171.2"), 117120);
}

TEST(Price, RejectsAnythingElse) {
    for (const char* bad : {"", "-1", "1.234", "abc", "1.", ".5", "1e5", "1,000", "12.3x"}) {
        EXPECT_FALSE(parse_price(bad).has_value()) << '"' << bad << '"';
    }
}

TEST(Price, FormatsWithTwoDecimals) {
    EXPECT_EQ(format_price(10150), "101.50");
    EXPECT_EQ(format_price(5), "0.05");
    EXPECT_EQ(format_price(100), "1.00");
    EXPECT_EQ(format_price(0), "0.00");
}

TEST(Price, FormatThenParseGivesTheSamePrice) {
    for (Price p = 0; p < 20000; p += 7) {
        EXPECT_EQ(parse_price(format_price(p)), p);
    }
}

ParsedScript parse(const std::string& text) {
    std::istringstream in(text);
    return parse_script(in);
}

TEST(Script, ReadsSettingsAndEventsInOrder) {
    const ParsedScript parsed = parse(
        "# a tiny morning\n"
        "PREV_CLOSE 100.00\n"
        "TICK 0.05\n"
        "BAND 90 110\n"
        "\n"
        "PHASE PREOPEN\n"
        "ADD 1 BUY LIMIT 101.50 300   # comment after an event\n"
        "ADD 2 SELL MARKET 50\n"
        "MODIFY 1 101.00 200\n"
        "CANCEL 2\n"
        "PHASE AUCTION\n");
    ASSERT_TRUE(parsed.errors.empty()) << parsed.errors.front().message;
    const Script& script = parsed.script;
    EXPECT_EQ(script.config.previous_close, 10000);
    EXPECT_EQ(script.config.tick_size, 5);
    EXPECT_EQ(script.config.lower_band, 9000);
    EXPECT_EQ(script.config.upper_band, 11000);
    ASSERT_EQ(script.events.size(), 6u);
    EXPECT_EQ(script.lines, (std::vector<int>{6, 7, 8, 9, 10, 11}));

    EXPECT_EQ(std::get<ChangePhase>(script.events[0]).phase, Phase::PreOpen);
    const Order& buy = std::get<AddOrder>(script.events[1]).order;
    EXPECT_EQ(buy, limit_order(1, Side::Buy, rs(101, 50), 300));
    const Order& sell = std::get<AddOrder>(script.events[2]).order;
    EXPECT_EQ(sell, market_order(2, Side::Sell, 50));
    const ModifyOrder& modify = std::get<ModifyOrder>(script.events[3]);
    EXPECT_EQ(modify.id, 1u);
    EXPECT_EQ(modify.price, rs(101));
    EXPECT_EQ(modify.quantity, 200);
    EXPECT_EQ(std::get<CancelOrder>(script.events[4]).id, 2u);
    EXPECT_EQ(std::get<ChangePhase>(script.events[5]).phase, Phase::Auction);
}

TEST(Script, ReportsBadLinesWithLineNumbers) {
    const ParsedScript parsed = parse(
        "PREV_CLOSE 100\n"
        "PHASE PREOPEN\n"
        "ADD 1 BUY LIMIT 101.555 10\n"
        "JUMP 3\n"
        "TICK 0.05\n"
        "ADD 2 HOLD LIMIT 100 10\n");
    ASSERT_EQ(parsed.errors.size(), 4u);
    EXPECT_EQ(parsed.errors[0].line, 3);  // three decimals
    EXPECT_EQ(parsed.errors[1].line, 4);  // unknown command
    EXPECT_EQ(parsed.errors[2].line, 5);  // settings after the first event
    EXPECT_EQ(parsed.errors[3].line, 6);  // not BUY or SELL
    EXPECT_EQ(parsed.script.events.size(), 1u) << "only the good lines become events";
}

TEST(Script, RequiresAPreviousClose) {
    const ParsedScript parsed = parse("PHASE PREOPEN\n");
    ASSERT_EQ(parsed.errors.size(), 1u);
    EXPECT_EQ(parsed.errors[0].message, "missing PREV_CLOSE");
}

}  // namespace
}  // namespace mini_nse
