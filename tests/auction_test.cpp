// Phase 1: the opening price. The nine paper cases from docs/test-cases.md, plus edge cases.
// Expected answers come from docs/test-cases-answers.md.
#include "paper_cases.h"

namespace mini_nse {
namespace {

using namespace paper;

void expect_result(const AuctionResult& result, Price price, Quantity quantity, Quantity imbalance,
                   DecidedBy rule) {
    ASSERT_TRUE(result.price.has_value()) << "expected a price";
    EXPECT_EQ(*result.price, price);
    EXPECT_EQ(result.quantity, quantity);
    EXPECT_EQ(result.imbalance, imbalance);
    EXPECT_EQ(result.decided_by, rule);
}

void expect_no_price(const AuctionResult& result) {
    EXPECT_FALSE(result.price.has_value());
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.decided_by, DecidedBy::NoPrice);
}

TEST(AuctionTable, Case1MatchesThePaperTable) {
    const std::vector<PriceLevelStats> expected = {
        {rs(99), 600, 150, 150, 450},
        {rs(100), 600, 350, 350, 250},
        {rs(101), 500, 600, 500, 100},
        {rs(102), 300, 600, 300, 300},
    };
    EXPECT_EQ(auction_table(case1()), expected);
}

TEST(Equilibrium, Case1_MostTradableWins) {
    expect_result(find_equilibrium(case1(), rs(100)), rs(101), 500, 100, DecidedBy::MaxTradable);
}

TEST(Equilibrium, Case2_MarketBuyAddsDemandAtEveryPrice) {
    expect_result(find_equilibrium(case2(), rs(100)), rs(101), 550, 50, DecidedBy::MaxTradable);
}

TEST(Equilibrium, Case3_LeastImbalanceBreaksTheTie) {
    // The previous close (105) is nearer to 101, but rule 2 decides before rule 3 is reached.
    expect_result(find_equilibrium(case3(), rs(105)), rs(100), 200, 0, DecidedBy::MinImbalance);
}

TEST(Equilibrium, Case4_NearestToPreviousCloseBreaksTheTie) {
    expect_result(find_equilibrium(case4(), rs(103)), rs(101), 100, 0, DecidedBy::NearestPreviousClose);
}

TEST(Equilibrium, Case4_NearestCanAlsoBeTheLowerPrice) {
    expect_result(find_equilibrium(case4(), rs(98)), rs(100), 100, 0, DecidedBy::NearestPreviousClose);
}

TEST(Equilibrium, Case5_HalfwayUsesThePreviousCloseItself) {
    expect_result(find_equilibrium(case4(), rs(100, 50)), rs(100, 50), 100, 0,
                  DecidedBy::HalfwayPreviousClose);
}

TEST(Equilibrium, Case6_OnlyMarketOrdersTradeAtThePreviousClose) {
    expect_result(find_equilibrium(case6(), rs(100)), rs(100), 80, 20, DecidedBy::MarketOrdersOnly);
}

TEST(Equilibrium, Case7_NoOverlapMeansNoPrice) {
    expect_no_price(find_equilibrium(case7(), rs(100)));
}

TEST(Equilibrium, Case8_EmptySideMeansNoPrice) {
    expect_no_price(find_equilibrium(case8(), rs(100)));
}

TEST(Equilibrium, Case9_PriceAndQuantity) {
    // Who gets filled (B3 first, then B1) is tested in auction_fill_test.cpp.
    expect_result(find_equilibrium(case9(), rs(100)), rs(100), 120, 130, DecidedBy::MaxTradable);
}

TEST(Equilibrium, EmptyBookHasNoPrice) {
    expect_no_price(find_equilibrium(AuctionBook{}, rs(100)));
}

TEST(Equilibrium, MarketOrdersOnOneSideOnlyHaveNoPrice) {
    expect_no_price(find_equilibrium(book_of({market_order(B1, Side::Buy, 100)}), rs(100)));
}

TEST(Equilibrium, MarketBuyAgainstLimitSells) {
    const AuctionBook book = book_of({market_order(B1, Side::Buy, 100),
                                      limit_order(S1, Side::Sell, rs(101), 50)});
    expect_result(find_equilibrium(book, rs(100)), rs(101), 50, 50, DecidedBy::MaxTradable);
}

TEST(Equilibrium, CancelledOrdersDoNotCount) {
    AuctionBook book = case1();
    ASSERT_TRUE(book.cancel(B1));  // the 300 @ 102 buyer leaves
    // Tradable becomes 150 at 99, 300 at 100 and 200 at 101, so the price drops from 101 to 100.
    expect_result(find_equilibrium(book, rs(100)), rs(100), 300, 50, DecidedBy::MaxTradable);
}

}  // namespace
}  // namespace mini_nse
