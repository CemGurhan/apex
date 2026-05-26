#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "apex/simfeeder.hpp"

namespace {

using namespace std::chrono_literals;

// Picks a config that funnels the feeder to exactly one order kind.
// Setting fields post-construction is important because DistConfig's
// limit_order_prob default expression isn't re-evaluated when sibling
// fields are later mutated.
DistConfig OnlyLimit(double aggressive_prob = 0.0, double sigma = 0.001) {
    DistConfig cfg{};
    cfg.market_order_prob = 0.0;
    cfg.limit_order_prob = 1.0;
    cfg.cancel_order_prob = 0.0;
    cfg.aggressive_prob = aggressive_prob;
    cfg.sigma = sigma;
    return cfg;
}

DistConfig OnlyMarket() {
    DistConfig cfg{};
    cfg.market_order_prob = 1.0;
    cfg.limit_order_prob = 0.0;
    cfg.cancel_order_prob = 0.0;
    return cfg;
}

DistConfig OnlyCancel() {
    DistConfig cfg{};
    cfg.market_order_prob = 0.0;
    cfg.limit_order_prob = 0.0;
    cfg.cancel_order_prob = 1.0;
    return cfg;
}

Order MakeLimitBuy(uint64_t client_id, double price, uint64_t qty) {
    return Order{client_id, qty, 0, price, Side::Buy, 0, OrderType::Limit};
}

Order MakeLimitSell(uint64_t client_id, double price, uint64_t qty) {
    return Order{client_id, qty, 0, price, Side::Sell, 0, OrderType::Limit};
}


TEST(SimulatedFeeder, RunsAndStopsWithoutCrashing) {
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    {
        OrderBookClient client(book, buffer);
        {
            SimulatedFeeder feeder(client);
            feeder.Run();
            std::this_thread::sleep_for(20ms);
            feeder.Stop();
        } // feeder dtor joins its thread
        std::this_thread::sleep_for(20ms); // let any in-flight events drain
    } // client dtor joins its reader

    SUCCEED();
}

TEST(SimulatedFeeder, LimitOnlyConfigRestsOrdersOnBook) {
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    {
        OrderBookClient client(book, buffer);
        {
            SimulatedFeeder feeder(client, {1, 2048}, 100.0, OnlyLimit());
            feeder.Run();
            std::this_thread::sleep_for(50ms);
            feeder.Stop();
        }
        std::this_thread::sleep_for(50ms);
    }

    // With passive limits clustered around fair=100, both sides should have rested orders.
    EXPECT_GT(book.GetBestBid(), 0.0);
    EXPECT_GT(book.GetBestAsk(), 0.0);
    EXPECT_LT(book.GetBestBid(), book.GetBestAsk()); // no cross
}

TEST(SimulatedFeeder, MarketOnlyAgainstSeededBookProducesTrades) {
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    std::atomic<int> trade_count{0};
    book.RegisterTradeEventAction([&](const Trade&) {
        trade_count.fetch_add(1, std::memory_order_relaxed);
    });

    // Pre-seed thick liquidity on both sides — the feeder's market orders will hit it.
    // High client_ids to avoid collisions with the feeder's ids (which start at 1).
    for (uint64_t i = 0; i < 50; ++i) {
        book.AddLimitOrder(MakeLimitBuy(100000 + i, 90.0 + static_cast<double>(i) * 0.1, 1000));
        book.AddLimitOrder(MakeLimitSell(200000 + i, 110.0 + static_cast<double>(i) * 0.1, 1000));
    }

    {
        OrderBookClient client(book, buffer);
        {
            SimulatedFeeder feeder(client, {1, 2048}, 100.0, OnlyMarket());
            feeder.Run();
            std::this_thread::sleep_for(50ms);
            feeder.Stop();
        }
        std::this_thread::sleep_for(50ms);
    }

    EXPECT_GT(trade_count.load(), 0);
}

TEST(SimulatedFeeder, CancelOnlyWithEmptyTrackingIsNoOp) {
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    std::atomic<int> trade_count{0};
    book.RegisterTradeEventAction([&](const Trade&) {
        trade_count.fetch_add(1, std::memory_order_relaxed);
    });

    {
        OrderBookClient client(book, buffer);
        {
            // The feeder hasn't placed any limits, so its active_order_ids is empty;
            // every cancel pick should be a no-op early return.
            SimulatedFeeder feeder(client, {1, 2048}, 100.0, OnlyCancel());
            feeder.Run();
            std::this_thread::sleep_for(50ms);
            feeder.Stop();
        }
        std::this_thread::sleep_for(50ms);
    }

    EXPECT_EQ(book.GetBestBid(), 0.0);
    EXPECT_EQ(book.GetBestAsk(), 0.0);
    EXPECT_EQ(trade_count.load(), 0);
}

TEST(SimulatedFeeder, StopHaltsBookActivity) {
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    {
        OrderBookClient client(book, buffer);
        {
            SimulatedFeeder feeder(client, {1, 2048}, 100.0, OnlyLimit());
            feeder.Run();
            std::this_thread::sleep_for(30ms);
            feeder.Stop();
        } // feeder dtor joins; no more events produced past this point

        // Drain in-flight events from the ring buffer, then snapshot.
        std::this_thread::sleep_for(50ms);
        auto bid_before = book.GetBestBid();
        auto ask_before = book.GetBestAsk();

        // No producer is running. Wait further; state must not change.
        std::this_thread::sleep_for(100ms);
        EXPECT_EQ(book.GetBestBid(), bid_before);
        EXPECT_EQ(book.GetBestAsk(), ask_before);
    }
}

TEST(SimulatedFeeder, MixedLimitAndCancelProgressesBookOverTime) {
    // The feeder places limits and occasionally cancels its own resting orders.
    // After a longer run, the book should have non-empty levels — the cancel path
    // shouldn't drain everything since limits dominate (limit_prob=0.7).
    OrderBook book(1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    DistConfig cfg{};
    cfg.market_order_prob = 0.0;
    cfg.limit_order_prob = 0.7;
    cfg.cancel_order_prob = 0.3;
    cfg.aggressive_prob = 0.0;
    cfg.sigma = 0.001;

    {
        OrderBookClient client(book, buffer);
        {
            SimulatedFeeder feeder(client, {1, 2048}, 100.0, cfg);
            feeder.Run();
            std::this_thread::sleep_for(100ms);
            feeder.Stop();
        }
        std::this_thread::sleep_for(50ms);
    }

    // At least one side should still have resting orders after the mix.
    EXPECT_TRUE(book.GetBestBid() > 0.0 || book.GetBestAsk() > 0.0);
}

}
