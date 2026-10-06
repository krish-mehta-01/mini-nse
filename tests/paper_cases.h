#pragma once

// The nine hand-solved cases from docs/test-cases.md, shared by every auction test.

#include <vector>

#include "test_helpers.h"

namespace mini_nse::paper {

// Order names match the paper: B = buy, S = sell.
constexpr OrderId B1 = 1, B2 = 2, B3 = 3, B4 = 4;
constexpr OrderId S1 = 11, S2 = 12, S3 = 13;

inline AuctionBook case1() {
    return book_of({limit_order(B1, Side::Buy, rs(102), 300),
                    limit_order(S1, Side::Sell, rs(99), 150),
                    limit_order(B2, Side::Buy, rs(101), 200),
                    limit_order(S2, Side::Sell, rs(100), 200),
                    limit_order(B3, Side::Buy, rs(100), 100),
                    limit_order(S3, Side::Sell, rs(101), 250)});
}

inline AuctionBook case2() {
    AuctionBook book = case1();
    EXPECT_TRUE(book.add(market_order(B4, Side::Buy, 50)));
    return book;
}

inline AuctionBook case3() {
    return book_of({limit_order(S1, Side::Sell, rs(100), 200),
                    limit_order(S2, Side::Sell, rs(101), 100),
                    limit_order(B1, Side::Buy, rs(101), 200)});
}

// Case 5 uses this book too, with a previous close of Rs 100.50.
inline AuctionBook case4() {
    return book_of({limit_order(S1, Side::Sell, rs(100), 100),
                    limit_order(B1, Side::Buy, rs(101), 100)});
}

inline AuctionBook case6() {
    return book_of({market_order(B1, Side::Buy, 100), market_order(S1, Side::Sell, 80)});
}

inline AuctionBook case7() {
    return book_of({limit_order(B1, Side::Buy, rs(99), 100),
                    limit_order(S1, Side::Sell, rs(101), 100)});
}

inline AuctionBook case8() {
    return book_of({limit_order(B1, Side::Buy, rs(101), 100),
                    limit_order(B2, Side::Buy, rs(100), 50)});
}

inline AuctionBook case9() {
    return book_of({limit_order(B1, Side::Buy, rs(100), 100),
                    limit_order(B2, Side::Buy, rs(100), 100),
                    limit_order(S1, Side::Sell, rs(100), 120),
                    market_order(B3, Side::Buy, 50)});
}

struct Case {
    int number = 0;
    AuctionBook book;
    Price previous_close = 0;
};

inline std::vector<Case> all() {
    return {{1, case1(), rs(100)}, {2, case2(), rs(100)}, {3, case3(), rs(105)},
            {4, case4(), rs(103)}, {5, case4(), rs(100, 50)}, {6, case6(), rs(100)},
            {7, case7(), rs(100)}, {8, case8(), rs(100)}, {9, case9(), rs(100)}};
}

}  // namespace mini_nse::paper
