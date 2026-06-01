#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "apex/orderbookclient.hpp"

namespace {

using namespace std::chrono_literals;

Order MakeLimitBuy(uint64_t client_id, double price, double qty) {
    return Order{client_id, qty, 0, price, Side::Buy, 0, OrderType::Limit};
}

Order MakeLimitSell(uint64_t client_id, double price, double qty) {
    return Order{client_id, qty, 0, price, Side::Sell, 0, OrderType::Limit};
}

Order MakeMarketBuy(uint64_t client_id, double qty) {
    return Order{client_id, qty, 0, 0.0, Side::Buy, 0, OrderType::Market};
}

OrderBookEvent LimitEvent(Order order) {
    return OrderBookEvent{OrderBookEventType::LimitOrder, order};
}

OrderBookEvent MarketEvent(Order order) {
    return OrderBookEvent{OrderBookEventType::MarketOrder, order};
}

// CancelEvent only needs the client_id populated.
OrderBookEvent CancelEvent(uint64_t client_id) {
    return OrderBookEvent{
        OrderBookEventType::CancelOrder,
        Order{client_id, 0, 0, 0.0, Side::Buy, 0, OrderType::Limit},
    };
}

template<typename F>
bool waitFor(F pred, std::chrono::milliseconds timeout = 2000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(100us);
    }
    return pred();
}


TEST(OrderBookClient, WriteReturnsTrueWhenBufferHasSpace) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    OrderBookClient client(book, buffer);

    EXPECT_TRUE(client.Write(LimitEvent(MakeLimitBuy(1, 100, 10))));
}

TEST(OrderBookClient, LimitBuyEventRestsAsBid) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade&) { trades.fetch_add(1, std::memory_order_release); });

    // sentinel ask used as a barrier — the tracer buy below crosses it
    book.AddLimitOrder(MakeLimitSell(99, 200, 1));

    {
        OrderBookClient client(book, buffer);

        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitBuy(1, 100, 10))));
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitBuy(2, 200, 1)))); // tracer

        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 1; }))
            << "tracer trade never fired — client did not process events";
    }

    EXPECT_EQ(book.GetBestBid(), 100);
    EXPECT_EQ(book.GetBestAsk(), 0); // sentinel ask was filled by tracer
}

TEST(OrderBookClient, LimitSellEventRestsAsAsk) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade&) { trades.fetch_add(1, std::memory_order_release); });

    book.AddLimitOrder(MakeLimitBuy(99, 1, 1)); // sentinel at price 1

    {
        OrderBookClient client(book, buffer);

        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitSell(1, 200, 5))));
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitSell(2, 1, 1)))); // tracer

        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 1; }));
    }

    EXPECT_EQ(book.GetBestAsk(), 200);
    EXPECT_EQ(book.GetBestBid(), 0);
}

TEST(OrderBookClient, MarketOrderEventFillsRestedLimit) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::atomic<int> trades{0};
    Trade captured{};
    std::mutex mu;
    book.RegisterTradeEventAction([&](const Trade& t) {
        std::lock_guard lock(mu);
        captured = t;
        trades.fetch_add(1, std::memory_order_release);
    });

    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    {
        OrderBookClient client(book, buffer);
        ASSERT_TRUE(client.Write(MarketEvent(MakeMarketBuy(2, 10))));
        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 1; }));
    }

    EXPECT_EQ(book.GetBestAsk(), 0);
    std::lock_guard lock(mu);
    EXPECT_EQ(captured.taker_client_id, 2);
    EXPECT_EQ(captured.maker_client_id, 1);
    EXPECT_EQ(captured.filled_quantity, 10);
    EXPECT_EQ(captured.price, 100);
}

TEST(OrderBookClient, CancelOrderEventRemovesRestedOrder) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade&) { trades.fetch_add(1, std::memory_order_release); });

    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));    // to be cancelled
    book.AddLimitOrder(MakeLimitSell(99, 200, 1));   // sentinel for tracer

    {
        OrderBookClient client(book, buffer);
        ASSERT_TRUE(client.Write(CancelEvent(1)));
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitBuy(2, 200, 1)))); // tracer

        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 1; }));
    }

    EXPECT_EQ(book.GetBestBid(), 0); // cancelled order removed
    EXPECT_EQ(book.GetBestAsk(), 0); // sentinel filled
}

TEST(OrderBookClient, ProcessesMixedEventsInFIFO) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::vector<Trade> captured;
    std::mutex mu;
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade& t) {
        std::lock_guard lock(mu);
        captured.push_back(t);
        trades.fetch_add(1, std::memory_order_release);
    });

    {
        OrderBookClient client(book, buffer);

        // 3 asks at ascending prices, then a market buy that crosses all of them
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitSell(1, 100, 5))));
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitSell(2, 101, 5))));
        ASSERT_TRUE(client.Write(LimitEvent(MakeLimitSell(3, 102, 5))));
        ASSERT_TRUE(client.Write(MarketEvent(MakeMarketBuy(4, 15))));

        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 3; }));
    }

    std::lock_guard lock(mu);
    ASSERT_EQ(captured.size(), 3u);
    EXPECT_EQ(captured[0].maker_client_id, 1);
    EXPECT_EQ(captured[1].maker_client_id, 2);
    EXPECT_EQ(captured[2].maker_client_id, 3);
    EXPECT_EQ(captured[0].price, 100);
    EXPECT_EQ(captured[1].price, 101);
    EXPECT_EQ(captured[2].price, 102);
}

TEST(OrderBookClient, ProcessesEventsArrivingAfterIdle) {
    // Exercises the reader's fallback to TryRead — after the 2048 spin loop the
    // reader parks on cv. A later Write must wake it (Push on empty -> notify).
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade&) { trades.fetch_add(1, std::memory_order_release); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    {
        OrderBookClient client(book, buffer);
        // give the reader enough time to exhaust spin retries and park on TryRead
        std::this_thread::sleep_for(50ms);

        ASSERT_TRUE(client.Write(MarketEvent(MakeMarketBuy(2, 10))));
        ASSERT_TRUE(waitFor([&] { return trades.load(std::memory_order_acquire) == 1; }));
    }

    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(OrderBookClient, StopHaltsProcessingOfFutureWrites) {
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    std::atomic<int> trades{0};
    book.RegisterTradeEventAction([&](const Trade&) { trades.fetch_add(1, std::memory_order_release); });

    {
        OrderBookClient client(book, buffer);
        client.Stop();
        // give the reader time to actually observe stop and exit
        std::this_thread::sleep_for(50ms);

        // writes now accumulate in the buffer but no one is reading
        ASSERT_TRUE(client.Write(MarketEvent(MakeMarketBuy(2, 10))));
        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(trades.load(std::memory_order_acquire), 0);
    }

    EXPECT_EQ(book.GetBestAsk(), 100); // book untouched
}

TEST(OrderBookClient, DestructorJoinsReaderThread) {
    // The OrderBookClient owns a jthread, whose destructor must request_stop
    // and join. If this test returns at all, the join completed.
    OrderBook book(1.0, 1.0);
    RingBuffer<OrderBookEvent, 1024> buffer;

    {
        OrderBookClient client(book, buffer);
        std::this_thread::sleep_for(10ms); // make sure the reader is actually running
    } // dtor must not hang

    SUCCEED();
}

}
