#include <gtest/gtest.h>
#include "apex/orderbook.hpp"

namespace {

RestingOrder MakeLimitBuy(int64_t id, int64_t price, int64_t qty) {
    return RestingOrder{id, qty, 0, price, Side::Buy, 0, OrderType::Limit};
}

RestingOrder MakeLimitSell(int64_t id, int64_t price, int64_t qty) {
    return RestingOrder{id, qty, 0, price, Side::Sell, 0, OrderType::Limit};
}

RestingOrder MakeMarketBuy(int64_t id, int64_t qty) {
    return RestingOrder{id, qty, 0, 0, Side::Buy, 0, OrderType::Market};
}

RestingOrder MakeMarketSell(int64_t id, int64_t qty) {
    return RestingOrder{id, qty, 0, 0, Side::Sell, 0, OrderType::Market};
}


TEST(OrderBook, EmptyBookBestBidIsZero) {
    OrderBook book;
    EXPECT_EQ(book.GetBestBid(), 0);
}

TEST(OrderBook, EmptyBookBestAskIsZero) {
    OrderBook book;
    EXPECT_EQ(book.GetBestAsk(), 0);
}


TEST(OrderBook, LimitBuyRestsOnEmptyBook) {
    OrderBook book;
    auto result = book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestBid(), 100);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(OrderBook, LimitSellRestsOnEmptyBook) {
    OrderBook book;
    auto result = book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 100);
    EXPECT_EQ(book.GetBestBid(), 0);
}


TEST(OrderBook, BestBidIsHighest) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 90, 5));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(3, 95, 5));

    EXPECT_EQ(book.GetBestBid(), 100);
}

TEST(OrderBook, BestAskIsLowest) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 110, 5));
    book.AddLimitOrder(MakeLimitSell(2, 100, 5));
    book.AddLimitOrder(MakeLimitSell(3, 105, 5));

    EXPECT_EQ(book.GetBestAsk(), 100);
}


TEST(OrderBook, LimitBuyFullyFilledByRestingSell) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    auto result = book.AddLimitOrder(MakeLimitBuy(2, 100, 10));

    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(book.GetBestAsk(), 0); // level cleared
}

TEST(OrderBook, LimitBuyPriceTooLowForSell) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 150, 10));

    auto result = book.AddLimitOrder(MakeLimitBuy(2, 100, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestBid() , 100);
    EXPECT_EQ(book.GetBestAsk(), 150);
}

TEST(OrderBook, LimitSellFullyFilledByRestingBuy) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    auto result = book.AddLimitOrder(MakeLimitSell(2, 100, 10));

    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(book.GetBestBid(), 0);
}

TEST(OrderBook, LimitSellPriceTooHighForBuy) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    auto result = book.AddLimitOrder(MakeLimitSell(2, 150, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestBid(), 100);
    EXPECT_EQ(book.GetBestAsk(), 150);
}



TEST(OrderBook, LimitBuyPartialFillRestsRemainder) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));

    auto result = book.AddLimitOrder(MakeLimitBuy(2, 100, 15));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(book.GetBestAsk(), 0);  // ask level cleared
    EXPECT_EQ(book.GetBestBid(), 100); // remainder rests as bid
}

TEST(OrderBook, LimitSellPartialFillRestsRemainder) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 5));

    auto result = book.AddLimitOrder(MakeLimitSell(2, 100, 15));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(book.GetBestBid(), 0);
    EXPECT_EQ(book.GetBestAsk(), 100);
}


TEST(OrderBook, LimitBuySmallerThanRestingSell) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 20));

    auto result = book.AddLimitOrder(MakeLimitBuy(2, 100, 5));

    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(book.GetBestAsk(), 100); // ask still has 15 remaining
}

TEST(OrderBook, LimitBuyCrossesMultipleAskLevels) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 101, 5));
    book.AddLimitOrder(MakeLimitSell(3, 102, 5));

    auto result = book.AddLimitOrder(MakeLimitBuy(4, 102, 12));

    EXPECT_EQ(result.filled_quantity, 12);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 102); // 3 remaining at 102
}

TEST(OrderBook, LimitSellCrossesMultipleBidLevels) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(2, 99, 5));
    book.AddLimitOrder(MakeLimitBuy(3, 98, 5));

    auto result = book.AddLimitOrder(MakeLimitSell(4, 98, 12));

    EXPECT_EQ(result.filled_quantity, 12);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestBid(), 98); // 3 remaining at 98
}

TEST(OrderBook, LimitBuyBelowBestAskRests) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 105, 10));

    auto result = book.AddLimitOrder(MakeLimitBuy(2, 100, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestBid(), 100);
    EXPECT_EQ(book.GetBestAsk(), 105);
}

TEST(OrderBook, LimitSellAboveBestBidRests) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 95, 10));

    auto result = book.AddLimitOrder(MakeLimitSell(2, 100, 10));

    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(book.GetBestBid(), 95);
    EXPECT_EQ(book.GetBestAsk(), 100);
}

TEST(OrderBook, FIFOPriorityWithinLevel) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10)); // first
    book.AddLimitOrder(MakeLimitSell(2, 100, 10)); // second

    // Buy 10 - should fill entirely against order 1
    book.AddLimitOrder(MakeLimitBuy(3, 100, 10));

    // Buy another 5 - should partially fill order 2
    auto result = book.AddLimitOrder(MakeLimitBuy(4, 100, 5));
    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 100); // 5 remaining from order 2
}

TEST(OrderBook, MarketBuyFillsAgainstAsks) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    auto result = book.AddMarketOrder(MakeMarketBuy(2, 10));

    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(OrderBook, MarketSellFillsAgainstBids) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    auto result = book.AddMarketOrder(MakeMarketSell(2, 10));

    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(book.GetBestBid(), 0);
}

TEST(OrderBook, MarketBuyCrossesMultipleLevels) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 101, 5));
    book.AddLimitOrder(MakeLimitSell(3, 102, 5));

    auto result = book.AddMarketOrder(MakeMarketBuy(4, 12));

    EXPECT_EQ(result.filled_quantity, 12);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 102); // 3 remaining at 102
}

TEST(OrderBook, MarketBuyPartialFillInsufficientLiquidity) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));

    auto result = book.AddMarketOrder(MakeMarketBuy(2, 20));

    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(result.quantity, 15); // unfilled remainder
    EXPECT_EQ(book.GetBestAsk(), 0);
    EXPECT_EQ(book.GetBestBid(), 0); // remainder does not rest on book since market order
}

TEST(OrderBook, MarketOrderOnEmptyBookNoFill) {
    OrderBook book;
    auto result = book.AddMarketOrder(MakeMarketBuy(1, 10));

    EXPECT_EQ(result.filled_quantity, 0);
    EXPECT_EQ(result.quantity, 10);
    EXPECT_EQ(book.GetBestBid(), 0); // market order does not rest on book
}

TEST(OrderBook, AddLimitOrderThrowsOnMarketType) {
    OrderBook book;
    EXPECT_THROW(book.AddLimitOrder(MakeMarketBuy(1, 10)), std::invalid_argument);
}

TEST(OrderBook, AddMarketOrderThrowsOnLimitType) {
    OrderBook book;
    EXPECT_THROW(book.AddMarketOrder(MakeLimitBuy(1, 100, 10)), std::invalid_argument);
}

TEST(OrderBook, EmptyLevelRemovedAfterFullFill) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    book.AddLimitOrder(MakeLimitSell(2, 105, 10));

    book.AddLimitOrder(MakeLimitBuy(3, 100, 10)); // wipe the 100 level

    EXPECT_EQ(book.GetBestAsk(), 105);
}

} 
