#include "test_helpers.h"

namespace mini_nse {
namespace {

TEST(AuctionBook, KeepsOrdersInArrivalOrder) {
    const AuctionBook book = book_of({limit_order(7, Side::Buy, rs(100), 10),
                                      market_order(3, Side::Sell, 5)});
    ASSERT_EQ(book.orders().size(), 2u);
    EXPECT_EQ(book.orders()[0].id, 7u);
    EXPECT_EQ(book.orders()[0].sequence, 1u);
    EXPECT_EQ(book.orders()[1].id, 3u);
    EXPECT_EQ(book.orders()[1].sequence, 2u);
}

TEST(AuctionBook, RejectsBadOrders) {
    AuctionBook book;
    ASSERT_TRUE(book.add(limit_order(1, Side::Buy, rs(100), 10)));
    EXPECT_FALSE(book.add(limit_order(1, Side::Sell, rs(101), 10))) << "duplicate id";
    EXPECT_FALSE(book.add(limit_order(2, Side::Buy, rs(100), 0))) << "zero quantity";
    EXPECT_FALSE(book.add(limit_order(3, Side::Buy, rs(100), -5))) << "negative quantity";
    EXPECT_FALSE(book.add(limit_order(4, Side::Buy, 0, 10))) << "limit order without a price";
    EXPECT_TRUE(book.add(market_order(5, Side::Buy, 10))) << "market orders need no price";
    EXPECT_EQ(book.orders().size(), 2u);
}

TEST(AuctionBook, CancelRemovesOnlyThatOrder) {
    AuctionBook book = book_of({limit_order(1, Side::Buy, rs(100), 10),
                                limit_order(2, Side::Sell, rs(101), 10)});
    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.find(1), nullptr);
    ASSERT_NE(book.find(2), nullptr);
    EXPECT_FALSE(book.cancel(1)) << "already gone";
    EXPECT_FALSE(book.cancel(99)) << "never existed";
}

TEST(AuctionBook, ReducingQuantityKeepsPriority) {
    AuctionBook book = book_of({limit_order(1, Side::Buy, rs(100), 10),
                                limit_order(2, Side::Buy, rs(100), 10)});
    ASSERT_TRUE(book.modify(1, rs(100), 4));
    EXPECT_EQ(book.orders()[0].id, 1u);
    EXPECT_EQ(book.orders()[0].quantity, 4);
    EXPECT_EQ(book.orders()[0].sequence, 1u);
}

TEST(AuctionBook, ChangingPriceLosesPriority) {
    AuctionBook book = book_of({limit_order(1, Side::Buy, rs(100), 10),
                                limit_order(2, Side::Buy, rs(100), 10)});
    ASSERT_TRUE(book.modify(1, rs(101), 10));
    EXPECT_EQ(book.orders()[0].id, 2u);
    EXPECT_EQ(book.orders()[1].id, 1u);
    EXPECT_EQ(book.orders()[1].price, rs(101));
    EXPECT_EQ(book.orders()[1].sequence, 3u);
}

TEST(AuctionBook, IncreasingQuantityLosesPriority) {
    AuctionBook book = book_of({limit_order(1, Side::Buy, rs(100), 10),
                                limit_order(2, Side::Buy, rs(100), 10)});
    ASSERT_TRUE(book.modify(1, rs(100), 11));
    EXPECT_EQ(book.orders()[0].id, 2u);
    EXPECT_EQ(book.orders()[1].id, 1u);
}

TEST(AuctionBook, MarketOrderModifyChangesOnlyQuantity) {
    AuctionBook book = book_of({market_order(1, Side::Sell, 10)});
    ASSERT_TRUE(book.modify(1, rs(500), 6));
    EXPECT_EQ(book.orders()[0].price, 0) << "market orders have no price";
    EXPECT_EQ(book.orders()[0].quantity, 6);
    EXPECT_EQ(book.orders()[0].sequence, 1u);
}

TEST(AuctionBook, ModifyRejectsUnknownOrInvalid) {
    AuctionBook book = book_of({limit_order(1, Side::Buy, rs(100), 10)});
    EXPECT_FALSE(book.modify(2, rs(100), 5)) << "unknown id";
    EXPECT_FALSE(book.modify(1, rs(100), 0)) << "zero quantity";
    EXPECT_FALSE(book.modify(1, 0, 5)) << "limit order without a price";
    EXPECT_EQ(book.orders()[0].quantity, 10) << "failed modify must change nothing";
}

}  // namespace
}  // namespace mini_nse
