// Phase 2: the normal market from 9:15, and the hand-over from the auction.
#include <string>

#include "mini_nse/order_book.h"
#include "paper_cases.h"

namespace mini_nse {
namespace {

// A book of orders that don't cross: every submit must be accepted and trade nothing.
OrderBook resting(std::initializer_list<Order> orders) {
    OrderBook book;
    for (const Order& order : orders) {
        const SubmitResult result = book.submit(order);
        EXPECT_TRUE(result.accepted) << "order " << order.id;
        EXPECT_TRUE(result.trades.empty()) << "order " << order.id << " was not meant to trade";
    }
    return book;
}

Order with_sequence(Order order, std::uint64_t sequence) {
    order.sequence = sequence;
    return order;
}

TEST(OrderBook, OrdersThatDontCrossWait) {
    const OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 100), limit_order(11, Side::Sell, rs(101), 50)});
    EXPECT_EQ(book.best_bid(), rs(100));
    EXPECT_EQ(book.best_ask(), rs(101));
    EXPECT_EQ(book.size(), 2u);
}

TEST(OrderBook, BuySweepsAsksByPriceThenTime) {
    OrderBook book = resting({limit_order(11, Side::Sell, rs(101), 100), limit_order(12, Side::Sell, rs(101), 50),
                              limit_order(13, Side::Sell, rs(102), 200)});
    const SubmitResult result = book.submit(limit_order(1, Side::Buy, rs(102), 180));
    ASSERT_TRUE(result.accepted);
    const std::vector<Trade> expected = {{1, 11, rs(101), 100}, {1, 12, rs(101), 50}, {1, 13, rs(102), 30}};
    EXPECT_EQ(result.trades, expected);
    EXPECT_EQ(book.best_ask(), rs(102));
    EXPECT_EQ(book.quantity_at(Side::Sell, rs(102)), 170);
    EXPECT_FALSE(book.best_bid().has_value()) << "fully filled, so nothing waits";
}

TEST(OrderBook, TradesAtTheWaitingOrdersPrice) {
    OrderBook book = resting({limit_order(11, Side::Sell, rs(101), 100)});
    const SubmitResult result = book.submit(limit_order(1, Side::Buy, rs(105), 100));
    EXPECT_EQ(result.trades, (std::vector<Trade>{{1, 11, rs(101), 100}})) << "willing to pay 105, pays 101";
}

TEST(OrderBook, SellSweepsBidsHighestFirst) {
    OrderBook book = resting({limit_order(2, Side::Buy, rs(99), 100), limit_order(1, Side::Buy, rs(100), 100)});
    const SubmitResult result = book.submit(limit_order(11, Side::Sell, rs(99), 150));
    EXPECT_EQ(result.trades, (std::vector<Trade>{{1, 11, rs(100), 100}, {2, 11, rs(99), 50}}));
    EXPECT_EQ(book.quantity_at(Side::Buy, rs(99)), 50);
}

TEST(OrderBook, LimitRemainderWaits) {
    OrderBook book = resting({limit_order(11, Side::Sell, rs(101), 40)});
    const SubmitResult result = book.submit(limit_order(1, Side::Buy, rs(101), 100));
    EXPECT_EQ(result.trades, (std::vector<Trade>{{1, 11, rs(101), 40}}));
    EXPECT_EQ(book.best_bid(), rs(101));
    EXPECT_EQ(book.quantity_at(Side::Buy, rs(101)), 60);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, MarketRemainderIsCancelled) {
    OrderBook book = resting({limit_order(11, Side::Sell, rs(101), 50)});
    const SubmitResult result = book.submit(market_order(1, Side::Buy, 80));
    EXPECT_EQ(result.trades, (std::vector<Trade>{{1, 11, rs(101), 50}}));
    EXPECT_EQ(book.size(), 0u) << "the unfilled 30 does not wait";
}

TEST(OrderBook, MarketOrderIntoAnEmptySideTradesNothing) {
    OrderBook book;
    const SubmitResult result = book.submit(market_order(1, Side::Sell, 10));
    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(book.size(), 0u);
}

TEST(OrderBook, RejectsBadOrders) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10)});
    EXPECT_FALSE(book.submit(limit_order(1, Side::Buy, rs(99), 10)).accepted) << "duplicate id";
    EXPECT_FALSE(book.submit(limit_order(2, Side::Buy, rs(99), 0)).accepted) << "zero quantity";
    EXPECT_FALSE(book.submit(limit_order(3, Side::Buy, 0, 10)).accepted) << "limit order without a price";
    EXPECT_EQ(book.size(), 1u);
}

TEST(OrderBook, CancelRemovesTheOrderAndItsEmptyLevel) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10), limit_order(2, Side::Buy, rs(99), 10)});
    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.best_bid(), rs(99)) << "the empty 100 level is gone";
    EXPECT_EQ(book.find(1), nullptr);
    EXPECT_FALSE(book.cancel(1)) << "already gone";
}

TEST(OrderBook, ReducingQuantityKeepsPriority) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10), limit_order(2, Side::Buy, rs(100), 10)});
    ASSERT_TRUE(book.modify(1, rs(100), 4).accepted);
    const std::vector<Order> bids = book.orders(Side::Buy);
    ASSERT_EQ(bids.size(), 2u);
    EXPECT_EQ(bids[0].id, 1u);
    EXPECT_EQ(bids[0].quantity, 4);
}

TEST(OrderBook, IncreasingQuantityGoesToTheBack) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10), limit_order(2, Side::Buy, rs(100), 10)});
    ASSERT_TRUE(book.modify(1, rs(100), 20).accepted);
    EXPECT_EQ(book.orders(Side::Buy)[0].id, 2u);
}

TEST(OrderBook, PriceChangeGoesToTheBackAndCanTradeAtOnce) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10), limit_order(2, Side::Buy, rs(100), 10),
                              limit_order(11, Side::Sell, rs(102), 5)});
    // Raising order 2 to 102 crosses the 102 seller: 5 trade immediately and the other 5 wait at 102.
    const SubmitResult result = book.modify(2, rs(102), 10);
    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(result.trades, (std::vector<Trade>{{2, 11, rs(102), 5}}));
    EXPECT_EQ(book.best_bid(), rs(102));
    EXPECT_EQ(book.quantity_at(Side::Buy, rs(102)), 5);
}

TEST(OrderBook, ModifyRejectsUnknownOrInvalid) {
    OrderBook book = resting({limit_order(1, Side::Buy, rs(100), 10)});
    EXPECT_FALSE(book.modify(2, rs(100), 5).accepted) << "unknown id";
    EXPECT_FALSE(book.modify(1, rs(100), 0).accepted) << "zero quantity";
    ASSERT_NE(book.find(1), nullptr);
    EXPECT_EQ(book.find(1)->quantity, 10) << "a rejected modify changes nothing";
}

TEST(OrderBook, RestoreKeepsOriginalTimePriority) {
    OrderBook book;
    ASSERT_TRUE(book.restore(with_sequence(limit_order(2, Side::Buy, rs(100), 100), 7)));
    ASSERT_TRUE(book.restore(with_sequence(limit_order(1, Side::Buy, rs(100), 30), 3)));  // arrived first
    ASSERT_TRUE(book.submit(limit_order(3, Side::Buy, rs(100), 10)).accepted);              // brand new
    const std::vector<Order> bids = book.orders(Side::Buy);
    ASSERT_EQ(bids.size(), 3u);
    EXPECT_EQ(bids[0].id, 1u);
    EXPECT_EQ(bids[1].id, 2u);
    EXPECT_EQ(bids[2].id, 3u);
    EXPECT_GT(bids[2].sequence, 7u) << "new orders queue behind everything carried over";
}

TEST(OrderBook, RestoreRefusesCrossingAndMarketOrders) {
    OrderBook book;
    ASSERT_TRUE(book.restore(with_sequence(limit_order(11, Side::Sell, rs(101), 10), 1)));
    EXPECT_FALSE(book.restore(with_sequence(limit_order(1, Side::Buy, rs(101), 10), 2))) << "would cross";
    EXPECT_FALSE(book.restore(with_sequence(market_order(2, Side::Buy, 10), 3))) << "market orders never wait";
    EXPECT_EQ(book.size(), 1u);
}

// The hand-over: auction leftovers open the normal market.

TEST(AuctionToMarket, Case1OpensAtBid100Ask101) {
    OrderBook book;
    for (const Order& order : run_auction(paper::case1(), rs(100)).leftovers) {
        ASSERT_TRUE(book.restore(order));
    }
    EXPECT_EQ(book.best_bid(), rs(100));
    EXPECT_EQ(book.best_ask(), rs(101));
}

TEST(AuctionToMarket, Case9LeftoverB1StaysAheadOfB2) {
    OrderBook book;
    for (const Order& order : run_auction(paper::case9(), rs(100)).leftovers) {
        ASSERT_TRUE(book.restore(order));
    }
    // A seller arriving at 9:15 fills B1 (sequence 1) before B2 (sequence 2).
    const SubmitResult result = book.submit(limit_order(21, Side::Sell, rs(100), 40));
    EXPECT_EQ(result.trades, (std::vector<Trade>{{paper::B1, 21, rs(100), 30}, {paper::B2, 21, rs(100), 10}}));
}

TEST(AuctionToMarket, EveryCaseHandsOverCleanly) {
    for (const paper::Case& c : paper::all()) {
        SCOPED_TRACE("case " + std::to_string(c.number));
        const AuctionOutcome outcome = run_auction(c.book, c.previous_close);
        OrderBook book;
        for (const Order& order : outcome.leftovers) {
            EXPECT_TRUE(book.restore(order)) << "order " << order.id;
        }
        EXPECT_EQ(book.size(), outcome.leftovers.size());
    }
}

}  // namespace
}  // namespace mini_nse
