#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "apex/stream/ringbuffer.hpp"

namespace {

TEST(RingBuffer, ReadOnEmptyReturnsFalse) {
    RingBuffer<int, 4> rb;
    auto [val, ok] = rb.Read();
    EXPECT_FALSE(ok);
    EXPECT_EQ(val, 0); // default-constructed T
}

TEST(RingBuffer, PushThenReadReturnsSameValue) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.Push(42));

    auto [val, ok] = rb.Read();
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 42);
}

TEST(RingBuffer, ReadAfterDrainReturnsFalse) {
    RingBuffer<int, 4> rb;
    rb.Push(1);
    rb.Read();

    auto [val, ok] = rb.Read();
    EXPECT_FALSE(ok);
}

TEST(RingBuffer, FIFOOrderPreserved) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.Push(1));
    EXPECT_TRUE(rb.Push(2));
    EXPECT_TRUE(rb.Push(3));

    auto a = rb.Read();
    auto b = rb.Read();
    auto c = rb.Read();

    ASSERT_TRUE(a.second);
    ASSERT_TRUE(b.second);
    ASSERT_TRUE(c.second);
    EXPECT_EQ(a.first, 1);
    EXPECT_EQ(b.first, 2);
    EXPECT_EQ(c.first, 3);
}

TEST(RingBuffer, PushReturnsFalseWhenFull) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.Push(10));
    EXPECT_TRUE(rb.Push(20));
    EXPECT_TRUE(rb.Push(30));
    EXPECT_TRUE(rb.Push(40));

    EXPECT_FALSE(rb.Push(50)); // full
}

TEST(RingBuffer, ReadOnFullBufferFreesSlot) {
    RingBuffer<int, 4> rb;
    rb.Push(10);
    rb.Push(20);
    rb.Push(30);
    rb.Push(40);
    EXPECT_FALSE(rb.Push(50));

    auto [val, ok] = rb.Read();
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 10);

    EXPECT_TRUE(rb.Push(50)); // slot freed
}

TEST(RingBuffer, WrapsAroundOnRepeatedFillDrain) {
    RingBuffer<int, 4> rb;

    // exercise indices well past Capacity to force wrap via mask
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(rb.Push(cycle * 10 + i));
        }
        for (int i = 0; i < 4; ++i) {
            auto [val, ok] = rb.Read();
            ASSERT_TRUE(ok);
            EXPECT_EQ(val, cycle * 10 + i);
        }
    }
}

TEST(RingBuffer, InterleavedPushReadPreservesOrder) {
    RingBuffer<int, 4> rb;
    rb.Push(1);
    rb.Push(2);

    auto a = rb.Read();
    EXPECT_EQ(a.first, 1);

    rb.Push(3);
    rb.Push(4);
    rb.Push(5); // crosses the wrap point (write_idx now 5)

    auto b = rb.Read();
    auto c = rb.Read();
    auto d = rb.Read();
    auto e = rb.Read();

    EXPECT_EQ(b.first, 2);
    EXPECT_EQ(c.first, 3);
    EXPECT_EQ(d.first, 4);
    EXPECT_EQ(e.first, 5);

    EXPECT_FALSE(rb.Read().second);
}

TEST(RingBuffer, CapacityOneHoldsSingleItem) {
    RingBuffer<int, 1> rb;
    EXPECT_TRUE(rb.Push(7));
    EXPECT_FALSE(rb.Push(8)); // full

    auto [val, ok] = rb.Read();
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 7);

    EXPECT_TRUE(rb.Push(8));
    EXPECT_EQ(rb.Read().first, 8);
}

TEST(RingBuffer, WorksWithNonTrivialType) {
    RingBuffer<std::string, 4> rb;
    EXPECT_TRUE(rb.Push(std::string("hello")));
    EXPECT_TRUE(rb.Push(std::string("world")));

    auto a = rb.Read();
    auto b = rb.Read();
    ASSERT_TRUE(a.second);
    ASSERT_TRUE(b.second);
    EXPECT_EQ(a.first, "hello");
    EXPECT_EQ(b.first, "world");
}

TEST(RingBuffer, PushMovesFromLvalueViaStdMove) {
    RingBuffer<std::string, 4> rb;
    std::string s = "movable";
    ASSERT_TRUE(rb.Push(std::move(s)));

    auto [val, ok] = rb.Read();
    ASSERT_TRUE(ok);
    EXPECT_EQ(val, "movable");
}

// ── TryRead ──

TEST(RingBufferTryRead, ReturnsImmediatelyWhenItemAvailable) {
    RingBuffer<int, 4> rb;
    rb.Push(99);

    std::stop_source src;
    auto [val, ok] = rb.TryRead(src.get_token());

    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 99);
}

TEST(RingBufferTryRead, ReturnsFalseWhenStopAlreadyRequested) {
    RingBuffer<int, 4> rb;
    std::stop_source src;
    src.request_stop();

    auto [val, ok] = rb.TryRead(src.get_token());
    EXPECT_FALSE(ok);
    EXPECT_EQ(val, 0);
}

TEST(RingBufferTryRead, BlocksUntilPushArrives) {
    RingBuffer<int, 4> rb;
    std::stop_source src;

    std::atomic<bool> reader_returned{false};
    int got = 0;
    bool got_ok = false;

    std::thread reader([&] {
        auto [val, ok] = rb.TryRead(src.get_token());
        got = val;
        got_ok = ok;
        reader_returned.store(true, std::memory_order_release);
    });

    // give the reader time to park on the cv
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_FALSE(reader_returned.load(std::memory_order_acquire))
        << "TryRead should still be blocked on empty buffer";

    // Push on an empty buffer routes through pushWithNotify -> cv.notify_one
    ASSERT_TRUE(rb.Push(123));

    reader.join();
    EXPECT_TRUE(got_ok);
    EXPECT_EQ(got, 123);
}

TEST(RingBufferTryRead, StopWakesBlockedReader) {
    RingBuffer<int, 4> rb;
    std::stop_source src;

    std::atomic<bool> reader_returned{false};
    bool got_ok = true;
    int got = -1;

    std::thread reader([&] {
        auto [val, ok] = rb.TryRead(src.get_token());
        got = val;
        got_ok = ok;
        reader_returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_FALSE(reader_returned.load(std::memory_order_acquire));

    src.request_stop(); // stop_callback should fire cv.notify_one
    reader.join();

    EXPECT_FALSE(got_ok);
    EXPECT_EQ(got, 0);
}

TEST(RingBufferTryRead, PreservesFIFOOrderingAcrossBlockingAndNonBlocking) {
    RingBuffer<int, 4> rb;
    std::stop_source src;

    // First TryRead hits the non-blocking path
    rb.Push(1);
    auto a = rb.TryRead(src.get_token());
    EXPECT_TRUE(a.second);
    EXPECT_EQ(a.first, 1);

    // Second TryRead must block (buffer empty) until producer pushes
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rb.Push(2);
        rb.Push(3);
    });

    auto b = rb.TryRead(src.get_token());
    EXPECT_TRUE(b.second);
    EXPECT_EQ(b.first, 2);

    // Third TryRead — item already buffered by producer, non-blocking path again
    auto c = rb.TryRead(src.get_token());
    EXPECT_TRUE(c.second);
    EXPECT_EQ(c.first, 3);

    producer.join();
}

TEST(RingBufferTryRead, OnlyEmptyToNonEmptyPushNotifies) {
    // Push on a non-empty buffer must NOT acquire the mutex / notify,
    // but a subsequent push on empty must still wake a blocked TryRead.
    RingBuffer<int, 4> rb;
    std::stop_source src;

    rb.Push(10); // empty -> non-empty (notifies, but no one listening)
    rb.Push(20); // non-empty -> non-empty (no notify path)

    // Drain via TryRead non-blocking path
    EXPECT_EQ(rb.TryRead(src.get_token()).first, 10);
    EXPECT_EQ(rb.TryRead(src.get_token()).first, 20);

    // Buffer is empty again. Block a reader and verify the next Push wakes it.
    std::atomic<bool> done{false};
    int got = 0;
    std::thread reader([&] {
        auto [val, ok] = rb.TryRead(src.get_token());
        got = val;
        done.store(ok, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_FALSE(done.load(std::memory_order_acquire));

    rb.Push(30);
    reader.join();
    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(got, 30);
}

TEST(RingBufferTryRead, BlockingProducerConsumerOrderedDelivery) {
    constexpr uint64_t kCapacity = 64;
    constexpr uint64_t kItems = 50'000;

    RingBuffer<uint64_t, kCapacity> rb;
    std::stop_source src;
    std::vector<uint64_t> consumed;
    consumed.reserve(kItems);
    std::atomic<uint64_t> consumed_count{0};

    std::thread producer([&] {
        for (uint64_t i = 0; i < kItems; ++i) {
            while (!rb.Push(uint64_t{i})) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        while (consumed_count.load(std::memory_order_relaxed) < kItems) {
            auto [val, ok] = rb.TryRead(src.get_token());
            if (!ok) break;
            consumed.push_back(val);
            consumed_count.fetch_add(1, std::memory_order_release);
        }
    });

    producer.join();
    // Wait for consumer to drain all items, then signal stop to release it
    // if it has parked on cv waiting for more.
    while (consumed_count.load(std::memory_order_acquire) < kItems) {
        std::this_thread::yield();
    }
    src.request_stop();
    consumer.join();

    ASSERT_EQ(consumed.size(), kItems);
    for (uint64_t i = 0; i < kItems; ++i) {
        ASSERT_EQ(consumed[i], i) << "ordering violated at index " << i;
    }
}

TEST(RingBuffer, SPSCConcurrentProducerConsumer) {
    constexpr uint64_t kCapacity = 1024;
    constexpr uint64_t kItems = 1'000'000;

    RingBuffer<uint64_t, kCapacity> rb;
    std::vector<uint64_t> consumed;
    consumed.reserve(kItems);

    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (uint64_t i = 0; i < kItems; ++i) {
            while (!rb.Push(uint64_t{i})) {
                // spin until consumer makes space
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        while (consumed.size() < kItems) {
            auto [val, ok] = rb.Read();
            if (ok) {
                consumed.push_back(val);
            } else if (producer_done.load(std::memory_order_acquire)) {
                // drain any tail
                auto [v2, ok2] = rb.Read();
                if (ok2) {
                    consumed.push_back(v2);
                } else {
                    break;
                }
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), kItems);
    for (uint64_t i = 0; i < kItems; ++i) {
        ASSERT_EQ(consumed[i], i) << "ordering violated at index " << i;
    }
}

}
