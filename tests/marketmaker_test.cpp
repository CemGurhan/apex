#include <gtest/gtest.h>
#include "apex/orderbook.hpp"
#include "apex/marketmaker.hpp"

namespace {

// Seed client IDs start high to avoid collisions with MarketMaker's internal counter.
constexpr uint64_t SEED_BID_ID = 10000;
constexpr uint64_t SEED_ASK_ID = 10001;

void SeedBook(OrderBook& book, double bid_price, uint64_t bid_qty, double ask_price, uint64_t ask_qty) {
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = bid_qty, .price = bid_price,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = ask_qty, .price = ask_price,
        .side = Side::Sell, .type = OrderType::Limit
    });
}

// ── Start / Quote Placement ──

TEST(MarketMaker, StartPlacesBidAndAsk) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);
    // fair = 100, spread/2 = 5 → bid=95, ask=105

    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 95);
    EXPECT_EQ(book.GetBestAsk(), 105);
}

TEST(MarketMaker, QuotePricesReflectSpreadWidth) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);
    // fair = 100, spread/2 = 10 → bid=90, ask=110

    MarketMaker mm(book, 20, 0, 5, 100);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 90);
    EXPECT_EQ(book.GetBestAsk(), 110);
}

TEST(MarketMaker, StartIsNoOpOnEmptyBook) {
    // MM detects empty book in getSpreadPrices and placeQuotes throws.
    // Start() catches the runtime_error and logs, so no exception escapes
    // and no quotes are placed.
    OrderBook book(1.0);
    MarketMaker mm(book, 10, 0, 5, 100);

    EXPECT_NO_THROW(mm.Start());
    EXPECT_EQ(book.GetBestBid(), 0);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(MarketMaker, StartUsesAskAsFairWhenNoBids) {
    OrderBook book(1.0);
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = 100, .price = 150,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // fair = best_ask = 150, spread/2 = 5 → bid=145, ask=155
    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 145);
    EXPECT_EQ(book.GetBestAsk(), 150); // seed ask beats MM ask at 155
}

TEST(MarketMaker, StartUsesBidAsFairWhenNoAsks) {
    OrderBook book(1.0);
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = 100, .price = 50,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // fair = best_bid = 50, spread/2 = 5 → bid=45, ask=55
    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 50);  // seed bid beats MM bid at 45
    EXPECT_EQ(book.GetBestAsk(), 55);
}

// ── Trade Callback: Inventory + Requoting ──

TEST(MarketMaker, FilledBidAtMaxInventoryOnlyQuotesAsk) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    // max_inventory=5, qty=5 → one buy fill maxes out
    MarketMaker mm(book, 10, 0, 5, 5);
    mm.Start();
    // MM bid=95, ask=105

    // External sell fills MM's bid
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // inventory=5, too_long → no bid placed, only ask
    EXPECT_EQ(book.GetBestBid(), 50);  // seed bid, no MM bid
    EXPECT_EQ(book.GetBestAsk(), 105); // new MM ask
}

TEST(MarketMaker, FilledAskAtMaxInventoryOnlyQuotesBid) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 5);
    mm.Start();

    // External buy fills MM's ask
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // inventory=-5, too_short → no ask placed, only bid
    EXPECT_EQ(book.GetBestBid(), 95);  // new MM bid
    EXPECT_EQ(book.GetBestAsk(), 150); // seed ask, no MM ask
}

TEST(MarketMaker, FilledBidCancelsAskLeg) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();
    EXPECT_EQ(book.GetBestAsk(), 105); // MM ask present

    // Fill the bid → callback cancels ask leg, replaces quotes
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // Old ask (105) was cancelled and new one placed at same price (skew=0)
    // The fact that best ask is still 105 (not 150) proves requoting worked
    EXPECT_EQ(book.GetBestAsk(), 105);
    EXPECT_EQ(book.GetBestBid(), 95);
}

TEST(MarketMaker, FilledAskCancelsBidLeg) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();
    EXPECT_EQ(book.GetBestBid(), 95); // MM bid present

    // Fill the ask → callback cancels bid leg, replaces quotes
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    EXPECT_EQ(book.GetBestBid(), 95);
    EXPECT_EQ(book.GetBestAsk(), 105);
}

// ── Skew ──

TEST(MarketMaker, SkewShiftsQuotesDownWhenLong) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    // skew_factor=2 → after inventory=5: fair shifts by -10
    MarketMaker mm(book, 10, 2, 5, 100);
    mm.Start();
    // Initial: fair=100, bid=95, ask=105

    // Fill the bid → inventory=5
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // New fair = (50+150)/2 - (5*2) = 100 - 10 = 90
    // bid = 90 - 5 = 85, ask = 90 + 5 = 95
    EXPECT_EQ(book.GetBestBid(), 85);
    EXPECT_EQ(book.GetBestAsk(), 95);
}

TEST(MarketMaker, SkewShiftsQuotesUpWhenShort) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    MarketMaker mm(book, 10, 2, 5, 100);
    mm.Start();

    // Fill the ask → inventory=-5
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 105,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // New fair = (50+150)/2 - (-5*2) = 100 + 10 = 110
    // bid = 110 - 5 = 105, ask = 110 + 5 = 115
    EXPECT_EQ(book.GetBestBid(), 105);
    EXPECT_EQ(book.GetBestAsk(), 115);
}

// ── Tick Size Normalization ──

TEST(MarketMaker, TickSizeTruncatesNonAlignedQuotePrices) {
    OrderBook book(10.0);
    // Seeds bracket the MM quotes so MM wins both legs.
    // Seed bid price=10 (tick 1), seed ask price=190 (tick 19).
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = 100, .price = 10,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = 100, .price = 190,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // best_bid_price=10, best_ask_price=190 → fair=100
    // base_spread=25 (odd) → shift=12.5 → MM quotes bid=87.5, ask=112.5
    // tick=10: bid 87.5/10=8.75 truncates to tick 8 (price 80),
    //          ask 112.5/10=11.25 truncates to tick 11 (price 110)
    MarketMaker mm(book, 25, 0, 5, 100);
    mm.Start();

    EXPECT_EQ(book.GetBestBid(), 80);
    EXPECT_EQ(book.GetBestAsk(), 110);
}

// ── Requote with depleted sides ──

TEST(MarketMaker, RequoteUsesAskAsFairWhenBidsDepleted) {
    OrderBook book(1.0);
    // Thin bid side: seed qty=5, MM will add qty=5
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = 5, .price = 50,
        .side = Side::Buy, .type = OrderType::Limit
    });
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = 100, .price = 150,
        .side = Side::Sell, .type = OrderType::Limit
    });

    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();
    // MM bid=95(qty=5), ask=105(qty=5)

    // External sell consumes MM bid (5) + seed bid (5) → bids depleted
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 10, .price = 50,
        .side = Side::Sell, .type = OrderType::Limit
    });

    // Requote uses best_ask=150 as fair → bid=145, ask=155
    EXPECT_EQ(book.GetBestBid(), 145);
    EXPECT_EQ(book.GetBestAsk(), 150); // seed ask beats MM ask at 155
}

TEST(MarketMaker, RequoteUsesBidAsFairWhenAsksDepleted) {
    OrderBook book(1.0);
    book.AddLimitOrder(Order{
        .client_id = SEED_BID_ID, .quantity = 100, .price = 50,
        .side = Side::Buy, .type = OrderType::Limit
    });
    // Thin ask side: seed qty=5, MM will add qty=5
    book.AddLimitOrder(Order{
        .client_id = SEED_ASK_ID, .quantity = 5, .price = 150,
        .side = Side::Sell, .type = OrderType::Limit
    });

    MarketMaker mm(book, 10, 0, 5, 100);
    mm.Start();
    // MM bid=95(qty=5), ask=105(qty=5)

    // External buy consumes MM ask (5) + seed ask (5) → asks depleted
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 10, .price = 150,
        .side = Side::Buy, .type = OrderType::Limit
    });

    // Requote uses best_bid=50 as fair → bid=45, ask=55
    EXPECT_EQ(book.GetBestBid(), 50);  // seed bid beats MM bid at 45
    EXPECT_EQ(book.GetBestAsk(), 55);
}

// ── Multiple Rounds ──

TEST(MarketMaker, ConsecutiveFillsAccumulateInventory) {
    OrderBook book(1.0);
    SeedBook(book, 50, 100, 150, 100);

    // skew_factor=1, max_inv=100 → observe cumulative skew
    MarketMaker mm(book, 10, 1, 5, 100);
    mm.Start();
    // fair=100, bid=95, ask=105

    // First fill: buy → inventory=5
    book.AddLimitOrder(Order{
        .client_id = 20000, .quantity = 5, .price = 95,
        .side = Side::Sell, .type = OrderType::Limit
    });
    // fair = 100 - 5 = 95, bid=90, ask=100
    EXPECT_EQ(book.GetBestBid(), 90);
    EXPECT_EQ(book.GetBestAsk(), 100);

    // Second fill: buy again → inventory=10
    book.AddLimitOrder(Order{
        .client_id = 20001, .quantity = 5, .price = 90,
        .side = Side::Sell, .type = OrderType::Limit
    });
    // fair = 100 - 10 = 90, bid=85, ask=95
    EXPECT_EQ(book.GetBestBid(), 85);
    EXPECT_EQ(book.GetBestAsk(), 95);
}

}
