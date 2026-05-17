#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "apex/stream/ringbufffer.hpp"

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
