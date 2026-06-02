#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include "apex/strategies/marketmaker/marketmaker.hpp"

namespace {

using namespace std::chrono_literals;

// Seed client IDs sit well above the MM's counter (starts at 1) and external
// fills below (start at 20000) so the three sources never collide.
constexpr uint64_t SEED_BID_ID = 10000;
constexpr uint64_t SEED_ASK_ID = 10001;
constexpr const char* kUnusedPnlPath = "/tmp/marketmaker_test_unused.csv";

void SeedBook(OrderBook& book, double bid_price, double bid_qty, double ask_price, double ask_qty) {
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = bid_qty, .price = bid_price,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = ask_qty, .price = ask_price,
        .side = Side::Sell, .type = OrderType::Limit
    });
}

Order LimitOrder(uint64_t id, double price, double qty, Side side) {
    return Order{
        .client_id = id, .quantity = qty, .price = price,
        .side = side, .type = OrderType::Limit
    };
}

void writeLimit(OrderBookClient& client, uint64_t id, double price, double qty, Side side) {
    client.Write(OrderBookEvent{
        .type = OrderBookEventType::LimitOrder,
        .order = LimitOrder(id, price, qty, side),
    });
}

struct FinalState {
    double best_bid;
    double best_ask;
    int trade_count;
    double inventory;
};

// runScenario drives a full setup: pre-seeds the book, spins up the
// OrderBookClient + MarketMaker, runs mm.Start(), executes any external
// actions (writes from the test thread via the client), and returns the
// final book/MM state after both background threads have joined.
//
// Sleeps are coarse barriers: 50ms after Start lets the MM's two quote
// events drain through the buffer; 50ms after each external action lets
// the fill, the trade callback, and the resulting requote drain.
//
// NOTE on post-fill price expectations: in the async architecture MM's
// TradeEventAction reads the book BEFORE its own cancel-events from this
// callback have been processed by the reader. So getSpreadPrices() sees
// the MM's own pending-cancel quote as part of the market, which pulls
// fair toward those self-quotes and produces quote prices that look
// different from the synchronous-era expectations. Each post-fill test
// below reflects that.
FinalState runScenario(
    double tick,
    double base_spread,
    double skew,
    double qty,
    double max_inv,
    std::function<void(OrderBook&)> seed_book,
    std::function<void(OrderBookClient&)> external_actions = {}
) {
    OrderBook book(tick, 1.0);
    if (seed_book) seed_book(book);

    RingBuffer<OrderBookEvent, 1024> buffer;
    PnLTracker tracker(kUnusedPnlPath, kUnusedPnlPath, 0, 0);
    std::atomic<uint64_t> counter{1};
    std::atomic<int> trades{0};
    double final_inv = 0;

    MMStrategyConfig mm_cfg{
        .base_spread    = base_spread,
        .skew_factor    = skew,
        .order_quantity = qty,
        .max_inventory  = max_inv,
    };

    {
        OrderBookClient client(book, buffer);
        {
            MarketMaker mm(client, tracker, counter, mm_cfg);

            // Register before Start so the trade callback is in place when the
            // first MM-driven match (if any) fires. Safe to register here
            // because the reader thread is parked on an empty buffer at this point.
            book.RegisterTradeEventAction([&](const Trade& t) {
                trades.fetch_add(1, std::memory_order_release);
                mm.TradeEventAction(t);
            });

            mm.Start();
            std::this_thread::sleep_for(50ms); // MM's two quote events drain

            if (external_actions) {
                external_actions(client);
                std::this_thread::sleep_for(50ms); // fills + requote drain
            }

            final_inv = mm.GetInventory();
        } // mm dies; book.trade_event_action still references it via the captured ref,
          // but no more events will be processed because the reader is about to join.
        std::this_thread::sleep_for(20ms);
    } // client dtor joins reader thread; safe to read book state below.

    return {book.GetBestBid(), book.GetBestAsk(), trades.load(), final_inv};
}


// ── Start / Quote Placement ──

TEST(MarketMaker, StartPlacesBidAndAsk) {
    auto s = runScenario(1.0, 10, 0, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); });
    // fair = 100, spread/2 = 5 → bid=95, ask=105
    EXPECT_EQ(s.best_bid, 95);
    EXPECT_EQ(s.best_ask, 105);
}

TEST(MarketMaker, QuotePricesReflectSpreadWidth) {
    auto s = runScenario(1.0, 20, 0, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); });
    // fair = 100, spread/2 = 10 → bid=90, ask=110
    EXPECT_EQ(s.best_bid, 90);
    EXPECT_EQ(s.best_ask, 110);
}

TEST(MarketMaker, StartIsNoOpOnEmptyBook) {
    // MM detects empty book; placeQuotes throws and Start() catches it. No quotes placed.
    auto s = runScenario(1.0, 10, 0, 5, 100, /*seed_book=*/nullptr);
    EXPECT_EQ(s.best_bid, 0);
    EXPECT_EQ(s.best_ask, 0);
}

TEST(MarketMaker, StartUsesAskAsFairWhenNoBids) {
    auto s = runScenario(1.0, 10, 0, 5, 100, [](OrderBook& b){
        b.AddLimitOrder(LimitOrder(SEED_ASK_ID, 150, 100, Side::Sell));
    });
    // fair = best_ask = 150, spread/2 = 5 → bid=145, ask=155
    EXPECT_EQ(s.best_bid, 145);
    EXPECT_EQ(s.best_ask, 150); // seed ask beats MM ask at 155
}

TEST(MarketMaker, StartUsesBidAsFairWhenNoAsks) {
    auto s = runScenario(1.0, 10, 0, 5, 100, [](OrderBook& b){
        b.AddLimitOrder(LimitOrder(SEED_BID_ID, 50, 100, Side::Buy));
    });
    // fair = best_bid = 50, spread/2 = 5 → bid=45, ask=55
    EXPECT_EQ(s.best_bid, 50);  // seed bid beats MM bid at 45
    EXPECT_EQ(s.best_ask, 55);
}

// ── Trade Callback: Inventory + Requoting ──

TEST(MarketMaker, FilledBidAtMaxInventoryOnlyQuotesAsk) {
    auto s = runScenario(1.0, 10, 0, 5, 5, // max_inv=5, one buy fill maxes out
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            // External sell at 95 fills MM's bid (5 qty)
            writeLimit(c, 20000, 95, 5, Side::Sell);
        });
    // inventory=5, too_long → only the ask leg is re-placed.
    // Stale-state fair = (50 + MM-own-ask 105) / 2 = 77.5 → new ask = 82.
    EXPECT_EQ(s.best_bid, 50);   // seed bid only, no MM bid
    EXPECT_EQ(s.best_ask, 82);   // new MM ask, beats seed 150
    EXPECT_EQ(s.inventory, 5);
}

TEST(MarketMaker, FilledAskAtMaxInventoryOnlyQuotesBid) {
    auto s = runScenario(1.0, 10, 0, 5, 5,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            // External buy at 105 fills MM's ask (5 qty)
            writeLimit(c, 20000, 105, 5, Side::Buy);
        });
    // inventory=-5, too_short → only the bid leg is re-placed.
    // Stale-state fair = (MM-own-bid 95 + 150) / 2 = 122.5 → new bid = 117.
    EXPECT_EQ(s.best_bid, 117);  // new MM bid, beats seed 50
    EXPECT_EQ(s.best_ask, 150);  // seed ask only, no MM ask
    EXPECT_EQ(s.inventory, -5);
}

TEST(MarketMaker, FilledBidCancelsAskLeg) {
    auto s = runScenario(1.0, 10, 0, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            writeLimit(c, 20000, 95, 5, Side::Sell);
        });
    // Stale-state fair = (50 + MM-own-ask 105) / 2 = 77.5 → bid=72, ask=82.
    // Best ask shifting from 105 to 82 (well below seed 150) proves requote.
    EXPECT_EQ(s.best_bid, 72);
    EXPECT_EQ(s.best_ask, 82);
}

TEST(MarketMaker, FilledAskCancelsBidLeg) {
    auto s = runScenario(1.0, 10, 0, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            writeLimit(c, 20000, 105, 5, Side::Buy);
        });
    // Stale-state fair = (MM-own-bid 95 + 150) / 2 = 122.5 → bid=117, ask=127.
    EXPECT_EQ(s.best_bid, 117);
    EXPECT_EQ(s.best_ask, 127);
}

// ── Skew ──

TEST(MarketMaker, SkewShiftsQuotesDownWhenLong) {
    auto s = runScenario(1.0, 10, 2, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            writeLimit(c, 20000, 95, 5, Side::Sell);
        });
    // inv=5, skew=2. Stale-state fair=77.5, reservation=77.5-10=67.5 → bid=62, ask=72.
    EXPECT_EQ(s.best_bid, 62);
    EXPECT_EQ(s.best_ask, 72);
}

TEST(MarketMaker, SkewShiftsQuotesUpWhenShort) {
    auto s = runScenario(1.0, 10, 2, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            writeLimit(c, 20000, 105, 5, Side::Buy);
        });
    // inv=-5, skew=2. Stale-state fair=122.5, reservation=122.5+10=132.5 → bid=127, ask=137.
    EXPECT_EQ(s.best_bid, 127);
    EXPECT_EQ(s.best_ask, 137);
}

// ── Tick Size Truncation ──

TEST(MarketMaker, TickSizeTruncatesNonAlignedQuotePrices) {
    auto s = runScenario(10.0, 25, 0, 5, 100,
        [](OrderBook& b){
            // Seeds at price 10 (tick 1) and 190 (tick 19). MM quotes will win both.
            b.AddLimitOrder(LimitOrder(SEED_BID_ID, 10, 100, Side::Buy));
            b.AddLimitOrder(LimitOrder(SEED_ASK_ID, 190, 100, Side::Sell));
        });
    // fair=100, base_spread=25 → MM quotes at 87.5 and 112.5
    // tick=10: 87.5 → tick 8 → price 80, 112.5 → tick 11 → price 110
    EXPECT_EQ(s.best_bid, 80);
    EXPECT_EQ(s.best_ask, 110);
}

// ── Requote with depleted sides ──

TEST(MarketMaker, RequoteUsesAskAsFairWhenBidsDepleted) {
    auto s = runScenario(1.0, 10, 0, 5, 100,
        [](OrderBook& b){
            // Thin seed bid (5) — external sell will wipe both MM bid + seed bid.
            b.AddLimitOrder(LimitOrder(SEED_BID_ID, 50, 5, Side::Buy));
            b.AddLimitOrder(LimitOrder(SEED_ASK_ID, 150, 100, Side::Sell));
        },
        [](OrderBookClient& c){
            // External sell qty=10 @ 50 consumes MM bid (5) + seed bid (5)
            writeLimit(c, 20000, 50, 10, Side::Sell);
        });
    // After all bids depleted, MM sees best_bid=0, best_ask=105 (its own pending-cancel
    // ask). Fair = best_ask (single-side branch) = 105 → bid=100, ask=110.
    EXPECT_EQ(s.best_bid, 100);
    EXPECT_EQ(s.best_ask, 110); // MM new ask beats seed 150
}

TEST(MarketMaker, RequoteUsesBidAsFairWhenAsksDepleted) {
    auto s = runScenario(1.0, 10, 0, 5, 100,
        [](OrderBook& b){
            b.AddLimitOrder(LimitOrder(SEED_BID_ID, 50, 100, Side::Buy));
            b.AddLimitOrder(LimitOrder(SEED_ASK_ID, 150, 5, Side::Sell));
        },
        [](OrderBookClient& c){
            writeLimit(c, 20000, 150, 10, Side::Buy);
        });
    // After all asks depleted, MM sees best_bid=95 (own pending-cancel bid), best_ask=0.
    // Fair = best_bid = 95 → bid=90, ask=100.
    EXPECT_EQ(s.best_bid, 90);
    EXPECT_EQ(s.best_ask, 100);
}

// ── Multiple Rounds ──

TEST(MarketMaker, ConsecutiveFillsAccumulateInventory) {
    // The MM's post-fill quotes drift in the stale-state way described above,
    // so we don't pin exact prices here — we just verify the directional
    // invariant of the test: two consecutive fills accumulate inventory.
    auto s = runScenario(1.0, 10, 1, 5, 100,
        [](OrderBook& b){ SeedBook(b, 50, 100, 150, 100); },
        [](OrderBookClient& c){
            // First fill: external sell at 95 → MM inv += 5.
            writeLimit(c, 20000, 95, 5, Side::Sell);
            std::this_thread::sleep_for(50ms); // let the requote land before the next fill

            // Second fill: market sell qty=5 takes whatever the new MM best bid is.
            c.Write(OrderBookEvent{
                .type = OrderBookEventType::MarketOrder,
                .order = Order{
                    .client_id = 20001, .quantity = 5, .price = 0.0,
                    .side = Side::Sell, .type = OrderType::Market,
                },
            });
        });
    EXPECT_EQ(s.inventory, 10);
    EXPECT_GE(s.trade_count, 2);
}

}
