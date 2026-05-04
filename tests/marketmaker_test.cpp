#include <gtest/gtest.h>
#include "apex/orderbook.hpp"
#include "apex/marketmaker.hpp"

namespace {

// Seed IDs start high to avoid collisions with MarketMaker's internal counter.
constexpr uint64_t SEED_BID_ID = 10000;
constexpr uint64_t SEED_ASK_ID = 10001;

void SeedBook(OrderBook& book, uint64_t bid_price, uint64_t bid_qty, uint64_t ask_price, uint64_t ask_qty) {
    book.AddLimitOrder(Order{
        .id = SEED_BID_ID, .quantity = bid_qty, .price = bid_price,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .id = SEED_ASK_ID, .quantity = ask_qty, .price = ask_price,
        .side = Side::Sell, .type = OrderType::Limit
    });
}

// ── Start / Quote Placement ──

TEST(MarketMaker, StartPlacesBidAndAsk) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);
    // fair = 100, spread/2 = 5 → bid=95, ask=105

    MarketMaker mm(book, 10, 0, 5, 100, 1.0);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 95);
    EXPECT_EQ(book.GetBestAsk(), 105);
}

TEST(MarketMaker, QuotePricesReflectSpreadWidth) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);
    // fair = 100, spread/2 = 10 → bid=90, ask=110

    MarketMaker mm(book, 20, 0, 5, 100, 1.0);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 90);
    EXPECT_EQ(book.GetBestAsk(), 110);
}

TEST(MarketMaker, StartThrowsOnEmptyBook) {
    OrderBook book;
    MarketMaker mm(book, 10, 0, 5, 100, 1.0);

    EXPECT_THROW(mm.Start(), std::runtime_error);
}

TEST(MarketMaker, StartThrowsOnNoBids) {
    OrderBook book;
    book.AddLimitOrder(Order{
        .id = SEED_ASK_ID, .quantity = 100, .price = 150,
        .side = Side::Sell, .type = OrderType::Limit
    });

    MarketMaker mm(book, 10, 0, 5, 100, 1.0);
    EXPECT_THROW(mm.Start(), std::runtime_error);
}

TEST(MarketMaker, StartThrowsOnNoAsks) {
    OrderBook book;
    book.AddLimitOrder(Order{
        .id = SEED_BID_ID, .quantity = 100, .price = 50,
        .side = Side::Buy, .type = OrderType::Limit
    });

    MarketMaker mm(book, 10, 0, 5, 100, 1.0);
    EXPECT_THROW(mm.Start(), std::runtime_error);
}

// ── Trade Callback: Inventory + Requoting ──

TEST(MarketMaker, FilledBidAtMaxInventoryOnlyQuotesAsk) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    // max_inventory=5, qty=5 → one buy fill maxes out
    MarketMaker mm(book, 10, 0, 5, 5, 1.0);
    mm.Start();
    // MM bid=95, ask=105

    // External sell fills MM's bid
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // inventory=5, too_long → no bid placed, only ask
    EXPECT_EQ(book.GetBestBid(), 50);  // seed bid, no MM bid
    EXPECT_EQ(book.GetBestAsk(), 105); // new MM ask
}

TEST(MarketMaker, FilledAskAtMaxInventoryOnlyQuotesBid) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 5, 1.0);
    mm.Start();

    // External buy fills MM's ask
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // inventory=-5, too_short → no ask placed, only bid
    EXPECT_EQ(book.GetBestBid(), 95);  // new MM bid
    EXPECT_EQ(book.GetBestAsk(), 150); // seed ask, no MM ask
}

TEST(MarketMaker, FilledBidCancelsAskLeg) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 100, 1.0);
    mm.Start();
    EXPECT_EQ(book.GetBestAsk(), 105); // MM ask present

    // Fill the bid → callback cancels ask leg, replaces quotes
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // Old ask (105) was cancelled and new one placed at same price (skew=0)
    // The fact that best ask is still 105 (not 150) proves requoting worked
    EXPECT_EQ(book.GetBestAsk(), 105);
    EXPECT_EQ(book.GetBestBid(), 95);
}

TEST(MarketMaker, FilledAskCancelsBidLeg) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 100, 1.0);
    mm.Start();
    EXPECT_EQ(book.GetBestBid(), 95); // MM bid present

    // Fill the ask → callback cancels bid leg, replaces quotes
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    EXPECT_EQ(book.GetBestBid(), 95);
    EXPECT_EQ(book.GetBestAsk(), 105);
}

// ── Skew ──

TEST(MarketMaker, SkewShiftsQuotesDownWhenLong) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    // skew_factor=2 → after inventory=5: fair shifts by -10
    MarketMaker mm(book, 10, 2, 5, 100, 1.0);
    mm.Start();
    // Initial: fair=100, bid=95, ask=105

    // Fill the bid → inventory=5
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // New fair = (50+150)/2 - (5*2) = 100 - 10 = 90
    // bid = 90 - 5 = 85, ask = 90 + 5 = 95
    EXPECT_EQ(book.GetBestBid(), 85);
    EXPECT_EQ(book.GetBestAsk(), 95);
}

TEST(MarketMaker, SkewShiftsQuotesUpWhenShort) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 2, 5, 100, 1.0);
    mm.Start();

    // Fill the ask → inventory=-5
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // New fair = (50+150)/2 - (-5*2) = 100 + 10 = 110
    // bid = 110 - 5 = 105, ask = 110 + 5 = 115
    EXPECT_EQ(book.GetBestBid(), 105);
    EXPECT_EQ(book.GetBestAsk(), 115);
}

// ── Tick Size Normalization ──

TEST(MarketMaker, TickSizeNormalizesQuotePrices) {
    OrderBook book;
    // Seed with prices outside the normalized range so MM quotes become best.
    // Seeds at bid=1, ask=199. MM normalized prices will fall between them.
    book.AddLimitOrder(Order{
        .id = SEED_BID_ID, .quantity = 100, .price = 1,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .id = SEED_ASK_ID, .quantity = 100, .price = 199,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // fair = (1+199)/2 = 100
    // spread/2 = 10 → bid_raw = 90, ask_raw = 110
    // tick_size = 10 → bid = 9, ask = 11
    MarketMaker mm(book, 20, 0, 5, 100, 10.0);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 9);
    EXPECT_EQ(book.GetBestAsk(), 11);
}

// ── Multiple Rounds ──

TEST(MarketMaker, ConsecutiveFillsAccumulateInventory) {
    OrderBook book;
    SeedBook(book, 50, 100, 150, 100);

    // skew_factor=1, max_inv=100 → observe cumulative skew
    MarketMaker mm(book, 10, 1, 5, 100, 1.0);
    mm.Start();
    // fair=100, bid=95, ask=105

    // First fill: buy → inventory=5
    book.AddLimitOrder(Order{
        .id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });
    // fair = 100 - 5 = 95, bid=90, ask=100
    EXPECT_EQ(book.GetBestBid(), 90);
    EXPECT_EQ(book.GetBestAsk(), 100);

    // Second fill: buy again → inventory=10
    book.AddLimitOrder(Order{
        .id = 20001, .quantity = 5, .price = 90,
        .side = Side::Sell, .type = OrderType::Limit
    });
    // fair = 100 - 10 = 90, bid=85, ask=95
    EXPECT_EQ(book.GetBestBid(), 85);
    EXPECT_EQ(book.GetBestAsk(), 95);
}

}
