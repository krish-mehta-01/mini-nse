// Phase 3: a whole morning, phase by phase.
#include "mini_nse/session.h"
#include "paper_cases.h"

namespace mini_nse {
namespace {

using namespace paper;

SessionConfig config_with_close(Price previous_close) {
    SessionConfig config;
    config.previous_close = previous_close;
    return config;
}

Event add(const Order& order) { return AddOrder{order}; }
Event phase(Phase p) { return ChangePhase{p}; }

std::vector<Reject> rejects_of(Session& session, std::initializer_list<Event> events) {
    std::vector<Reject> rejects;
    for (const Event& event : events) {
        rejects.push_back(session.apply(event).reject);
    }
    return rejects;
}

TEST(Session, Case1MorningFromPreOpenToNormalMarket) {
    Session session(config_with_close(rs(100)));
    ASSERT_EQ(session.apply(phase(Phase::PreOpen)).reject, Reject::None);
    const AuctionBook book = case1();  // must outlive the loop: looping over case1().orders() would dangle
    for (const Order& order : book.orders()) {
        ASSERT_EQ(session.apply(add(order)).reject, Reject::None);
    }
    ASSERT_EQ(session.apply(phase(Phase::LimitOnly)).reject, Reject::None);

    const EventResult auction = session.apply(phase(Phase::Auction));  // the random close
    const std::vector<Trade> expected = {{B1, S1, rs(101), 150}, {B1, S2, rs(101), 150},
                                         {B2, S2, rs(101), 50}, {B2, S3, rs(101), 150}};
    EXPECT_EQ(auction.trades, expected);
    ASSERT_TRUE(session.auction().has_value());
    EXPECT_EQ(session.auction()->result.price, rs(101));

    ASSERT_EQ(session.apply(phase(Phase::Continuous)).reject, Reject::None);
    EXPECT_EQ(session.order_book().best_bid(), rs(100));
    EXPECT_EQ(session.order_book().best_ask(), rs(101));

    // 9:15: a seller of 40 at 100 meets B3, who carried over from the auction.
    const EventResult live = session.apply(add(limit_order(21, Side::Sell, rs(100), 40)));
    EXPECT_EQ(live.trades, (std::vector<Trade>{{B3, 21, rs(100), 40}}));
}

TEST(Session, MarketOrdersAreLockedAfterTheCutoff) {
    Session session(config_with_close(rs(100)));
    const auto rejects = rejects_of(session, {
        phase(Phase::PreOpen),
        add(market_order(1, Side::Buy, 50)),          // fine before 9:05
        phase(Phase::LimitOnly),
        add(market_order(2, Side::Sell, 50)),         // no new market orders
        CancelOrder{1},                               // can't cancel one either
        ModifyOrder{1, 0, 20},                        // or change it
        add(limit_order(3, Side::Sell, rs(101), 10)), // limit orders are still fine
        CancelOrder{3},
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::None, Reject::None, Reject::None, Reject::MarketOrdersClosed,
                                            Reject::MarketOrdersClosed, Reject::MarketOrdersClosed, Reject::None,
                                            Reject::None}));
}

TEST(Session, OrdersOnlyWhenTheMarketTakesThem) {
    Session session(config_with_close(rs(100)));
    const auto rejects = rejects_of(session, {
        add(limit_order(1, Side::Buy, rs(100), 10)),   // before 9:00
        phase(Phase::PreOpen),
        phase(Phase::Auction),
        add(limit_order(2, Side::Buy, rs(100), 10)),   // during the 9:12-9:15 buffer
        CancelOrder{2},
        ModifyOrder{2, rs(100), 5},
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::WrongPhase, Reject::None, Reject::None, Reject::WrongPhase,
                                            Reject::WrongPhase, Reject::WrongPhase}));
}

TEST(Session, PhasesGoInOrder) {
    Session session(config_with_close(rs(100)));
    const auto rejects = rejects_of(session, {
        phase(Phase::Auction),      // can't skip pre-open
        phase(Phase::PreOpen),
        phase(Phase::PreOpen),      // already there
        phase(Phase::Continuous),   // the auction has to happen first
        phase(Phase::Auction),      // straight from pre-open is fine (the old rules had no 9:05 cutoff)
        phase(Phase::Continuous),
        phase(Phase::LimitOnly),    // no going back
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::BadTransition, Reject::None, Reject::BadTransition,
                                            Reject::BadTransition, Reject::None, Reject::None,
                                            Reject::BadTransition}));
    EXPECT_EQ(session.phase(), Phase::Continuous);
}

TEST(Session, ChecksTickSizeAndPriceBand) {
    SessionConfig config = config_with_close(rs(100));
    config.tick_size = 5;  // Rs 0.05
    config.lower_band = rs(90);
    config.upper_band = rs(110);
    Session session(config);
    const auto rejects = rejects_of(session, {
        phase(Phase::PreOpen),
        add(limit_order(1, Side::Buy, rs(100, 2), 10)),   // 100.02 isn't a multiple of 0.05
        add(limit_order(2, Side::Buy, rs(111), 10)),      // above the band
        add(limit_order(3, Side::Buy, rs(89, 95), 10)),   // below the band
        add(limit_order(4, Side::Buy, rs(100, 5), 10)),   // fine
        ModifyOrder{4, rs(100, 3), 10},                   // off tick
        add(market_order(5, Side::Buy, 10)),              // market orders have no price to check
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::None, Reject::OffTick, Reject::OutsideBand, Reject::OutsideBand,
                                            Reject::None, Reject::OffTick, Reject::None}));
}

TEST(Session, IdsAreUniqueForTheWholeDay) {
    Session session(config_with_close(rs(100)));
    const auto rejects = rejects_of(session, {
        phase(Phase::PreOpen),
        add(limit_order(1, Side::Buy, rs(100), 10)),
        add(limit_order(1, Side::Sell, rs(101), 10)),     // same id, same morning
        CancelOrder{1},
        add(limit_order(1, Side::Buy, rs(100), 10)),      // still taken after a cancel
        phase(Phase::Auction),
        phase(Phase::Continuous),
        add(limit_order(1, Side::Buy, rs(100), 10)),      // and in the normal market
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::None, Reject::None, Reject::DuplicateId, Reject::None,
                                            Reject::DuplicateId, Reject::None, Reject::None, Reject::DuplicateId}));
}

TEST(Session, RejectsInvalidAndUnknownOrders) {
    Session session(config_with_close(rs(100)));
    const auto rejects = rejects_of(session, {
        phase(Phase::PreOpen),
        add(limit_order(1, Side::Buy, rs(100), 0)),
        add(limit_order(2, Side::Buy, 0, 10)),
        CancelOrder{99},
        ModifyOrder{99, rs(100), 10},
        add(limit_order(3, Side::Buy, rs(100), 10)),
        ModifyOrder{3, rs(100), -1},
    });
    EXPECT_EQ(rejects, (std::vector<Reject>{Reject::None, Reject::InvalidOrder, Reject::InvalidOrder,
                                            Reject::UnknownOrder, Reject::UnknownOrder, Reject::None,
                                            Reject::InvalidOrder}));
}

// The same events must always give exactly the same results: no clocks, no randomness, no hidden state.
TEST(Session, ReplayingTheSameMorningGivesIdenticalResults) {
    const std::vector<Event> morning = {
        phase(Phase::PreOpen),
        add(limit_order(1, Side::Buy, rs(102), 300)), add(limit_order(11, Side::Sell, rs(99), 150)),
        add(market_order(2, Side::Buy, 50)), add(limit_order(12, Side::Sell, rs(101), 400)),
        ModifyOrder{1, rs(101), 250}, phase(Phase::LimitOnly), CancelOrder{11},
        phase(Phase::Auction), phase(Phase::Continuous),
        add(limit_order(21, Side::Buy, rs(101), 70)), add(market_order(22, Side::Sell, 30)),
    };
    const auto run = [&morning] {
        Session session(config_with_close(rs(100)));
        std::vector<EventResult> results;
        for (const Event& event : morning) {
            results.push_back(session.apply(event));
        }
        return std::pair{results, session.order_book().orders(Side::Sell)};
    };
    EXPECT_EQ(run(), run());
}

}  // namespace
}  // namespace mini_nse
