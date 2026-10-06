// Phase 2: who gets the shares in the auction, and what moves on to the normal market.
// Expected trades come from docs/test-cases-answers.md.
#include <string>

#include "paper_cases.h"

namespace mini_nse {
namespace {

using namespace paper;

// An order as it should arrive in the normal market: a limit order with its original arrival sequence.
Order leftover(OrderId id, Side side, Price price, Quantity quantity, std::uint64_t sequence) {
    Order order = limit_order(id, side, price, quantity);
    order.sequence = sequence;
    return order;
}

TEST(AuctionFill, Case1_BetterPricesFillFirst) {
    const AuctionOutcome out = run_auction(case1(), rs(100));
    const std::vector<Trade> trades = {{B1, S1, rs(101), 150}, {B1, S2, rs(101), 150},
                                       {B2, S2, rs(101), 50}, {B2, S3, rs(101), 150}};
    EXPECT_EQ(out.trades, trades);
    const std::vector<Order> leftovers = {leftover(B3, Side::Buy, rs(100), 100, 5),
                                          leftover(S3, Side::Sell, rs(101), 100, 6)};
    EXPECT_EQ(out.leftovers, leftovers);
}

TEST(AuctionFill, Case2_MarketBuyFillsFirst) {
    const AuctionOutcome out = run_auction(case2(), rs(100));
    const std::vector<Trade> trades = {{B4, S1, rs(101), 50}, {B1, S1, rs(101), 100},
                                       {B1, S2, rs(101), 200}, {B2, S3, rs(101), 200}};
    EXPECT_EQ(out.trades, trades);
    const std::vector<Order> leftovers = {leftover(B3, Side::Buy, rs(100), 100, 5),
                                          leftover(S3, Side::Sell, rs(101), 50, 6)};
    EXPECT_EQ(out.leftovers, leftovers);
}

TEST(AuctionFill, Case3_SellerAt101IsNotWillingAt100) {
    const AuctionOutcome out = run_auction(case3(), rs(105));
    EXPECT_EQ(out.trades, (std::vector<Trade>{{B1, S1, rs(100), 200}}));
    EXPECT_EQ(out.leftovers, (std::vector<Order>{leftover(S2, Side::Sell, rs(101), 100, 2)}));
}

TEST(AuctionFill, Case5_TradesAtAPriceNobodyQuoted) {
    const AuctionOutcome out = run_auction(case4(), rs(100, 50));
    EXPECT_EQ(out.trades, (std::vector<Trade>{{B1, S1, rs(100, 50), 100}}));
    EXPECT_TRUE(out.leftovers.empty());
}

TEST(AuctionFill, Case6_LeftoverMarketOrderBecomesALimitAtTheAuctionPrice) {
    const AuctionOutcome out = run_auction(case6(), rs(100));
    EXPECT_EQ(out.trades, (std::vector<Trade>{{B1, S1, rs(100), 80}}));
    EXPECT_EQ(out.leftovers, (std::vector<Order>{leftover(B1, Side::Buy, rs(100), 20, 1)}));
}

TEST(AuctionFill, Case7_NoPriceMeansEverythingMovesOn) {
    const AuctionOutcome out = run_auction(case7(), rs(100));
    EXPECT_TRUE(out.trades.empty());
    const std::vector<Order> leftovers = {leftover(B1, Side::Buy, rs(99), 100, 1),
                                          leftover(S1, Side::Sell, rs(101), 100, 2)};
    EXPECT_EQ(out.leftovers, leftovers);
}

TEST(AuctionFill, Case9_MarketOrderArrivedLastButFillsFirst) {
    const AuctionOutcome out = run_auction(case9(), rs(100));
    const std::vector<Trade> trades = {{B3, S1, rs(100), 50}, {B1, S1, rs(100), 70}};
    EXPECT_EQ(out.trades, trades);
    // B1 keeps sequence 1, so it stays ahead of B2 in the normal market.
    const std::vector<Order> leftovers = {leftover(B1, Side::Buy, rs(100), 30, 1),
                                          leftover(B2, Side::Buy, rs(100), 100, 2)};
    EXPECT_EQ(out.leftovers, leftovers);
}

TEST(AuctionFill, NoPriceMarketOrderMovesOnAtThePreviousClose) {
    const AuctionBook book = book_of({market_order(B1, Side::Buy, 50), limit_order(B2, Side::Buy, rs(99), 100)});
    const AuctionOutcome out = run_auction(book, rs(100));
    EXPECT_TRUE(out.trades.empty());
    const std::vector<Order> leftovers = {leftover(B1, Side::Buy, rs(100), 50, 1),
                                          leftover(B2, Side::Buy, rs(99), 100, 2)};
    EXPECT_EQ(out.leftovers, leftovers);
}

// Rules that must hold for every case, whatever the numbers.
TEST(AuctionFill, EveryCaseConservesSharesAndLeavesNoCrossedBook) {
    for (const Case& c : all()) {
        SCOPED_TRACE("case " + std::to_string(c.number));
        const AuctionOutcome out = run_auction(c.book, c.previous_close);

        Quantity traded = 0;
        for (const Trade& trade : out.trades) {
            ASSERT_TRUE(out.result.price.has_value());
            EXPECT_EQ(trade.price, *out.result.price) << "every auction trade is at the one price";
            EXPECT_GT(trade.quantity, 0);
            traded += trade.quantity;
        }
        EXPECT_EQ(traded, out.result.quantity);

        // Each share is either traded or left over, on both sides.
        Quantity bought = 0, sold = 0, left_buy = 0, left_sell = 0;
        for (const Order& order : c.book.orders()) {
            (order.side == Side::Buy ? bought : sold) += order.quantity;
        }
        for (const Order& order : out.leftovers) {
            EXPECT_EQ(order.type, OrderType::Limit) << "nothing reaches the normal market as a market order";
            (order.side == Side::Buy ? left_buy : left_sell) += order.quantity;
        }
        EXPECT_EQ(bought, traded + left_buy);
        EXPECT_EQ(sold, traded + left_sell);

        // If buyers and sellers were still willing to trade with each other, the auction missed a trade.
        for (const Order& buy : out.leftovers) {
            for (const Order& sell : out.leftovers) {
                if (buy.side == Side::Buy && sell.side == Side::Sell) {
                    EXPECT_LT(buy.price, sell.price) << "buy " << buy.id << " crosses sell " << sell.id;
                }
            }
        }
    }
}

}  // namespace
}  // namespace mini_nse
