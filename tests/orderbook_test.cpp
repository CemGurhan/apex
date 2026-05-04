#include <gtest/gtest.h>
#include "apex/orderbook.hpp"

namespace {

Order MakeLimitBuy(uint64_t id, uint64_t price, uint64_t qty) {
    return Order{id, qty, 0, price, Side::Buy, 0, OrderType::Limit};
}

Order MakeLimitSell(uint64_t id, uint64_t price, uint64_t qty) {
    return Order{id, qty, 0, price, Side::Sell, 0, OrderType::Limit};
}

Order MakeMarketBuy(uint64_t id, uint64_t qty) {
    return Order{id, qty, 0, 0, Side::Buy, 0, OrderType::Market};
}

Order MakeMarketSell(uint64_t id, uint64_t qty) {
    return Order{id, qty, 0, 0, Side::Sell, 0, OrderType::Market};
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

// ── Cancel Order ──

TEST(CancelOrder, CancelRestingBidReturnsOrderSnapshot) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    auto cancelled = book.CancelOrder(1);

    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->id, 1);
    EXPECT_EQ(cancelled->price, 100);
    EXPECT_EQ(cancelled->quantity, 10);
    EXPECT_EQ(cancelled->filled_quantity, 0);
    EXPECT_EQ(cancelled->side, Side::Buy);
}

TEST(CancelOrder, CancelRestingAskReturnsOrderSnapshot) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    auto cancelled = book.CancelOrder(1);

    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->id, 1);
    EXPECT_EQ(cancelled->price, 100);
    EXPECT_EQ(cancelled->quantity, 10);
    EXPECT_EQ(cancelled->side, Side::Sell);
}

TEST(CancelOrder, CancelRemovesBidFromBook) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    book.CancelOrder(1);

    EXPECT_EQ(book.GetBestBid(), 0);
}

TEST(CancelOrder, CancelRemovesAskFromBook) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    book.CancelOrder(1);

    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(CancelOrder, ReturnsNulloptOnNonExistentOrderId) {
    OrderBook book;

    EXPECT_FALSE(book.CancelOrder(999).has_value());
}

TEST(CancelOrder, CancelBestBidRevealsNextBest) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(2, 90, 5));

    book.CancelOrder(1);

    EXPECT_EQ(book.GetBestBid(), 90);
}

TEST(CancelOrder, CancelBestAskRevealsNextBest) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 110, 5));

    book.CancelOrder(1);

    EXPECT_EQ(book.GetBestAsk(), 110);
}

TEST(CancelOrder, CancelOneOfTwoAtSameLevelKeepsLevel) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 100, 5));

    book.CancelOrder(1);

    EXPECT_EQ(book.GetBestAsk(), 100); // level still exists with order 2
}

TEST(CancelOrder, CancelHeadLeavesRemainingMatchable) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5)); // head
    book.AddLimitOrder(MakeLimitSell(2, 100, 10)); // tail

    book.CancelOrder(1); // remove head

    // order 2 should still be matchable
    auto result = book.AddLimitOrder(MakeLimitBuy(3, 100, 10));
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(CancelOrder, CancelTailLeavesHeadMatchable) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5)); // head
    book.AddLimitOrder(MakeLimitSell(2, 100, 10)); // tail

    book.CancelOrder(2); // remove tail

    auto result = book.AddLimitOrder(MakeLimitBuy(3, 100, 5));
    EXPECT_EQ(result.filled_quantity, 5);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(CancelOrder, CancelMiddleOfThreeOrderLevel) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));  // head
    book.AddLimitOrder(MakeLimitSell(2, 100, 5));  // middle
    book.AddLimitOrder(MakeLimitSell(3, 100, 5));  // tail

    book.CancelOrder(2); // remove middle

    // head and tail should still be matchable in FIFO order
    auto result = book.AddLimitOrder(MakeLimitBuy(4, 100, 10));
    EXPECT_EQ(result.filled_quantity, 10);
    EXPECT_EQ(result.quantity, 0);
    EXPECT_EQ(book.GetBestAsk(), 0);
}

TEST(CancelOrder, CancelPartiallyFilledOrderReturnsCorrectSnapshot) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 20));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 5)); // partially fills order 1

    auto cancelled = book.CancelOrder(1);

    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->quantity, 15);
    EXPECT_EQ(cancelled->filled_quantity, 5);
}

TEST(CancelOrder, DoubleCancelReturnsNullopt) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitBuy(1, 100, 10));

    ASSERT_TRUE(book.CancelOrder(1).has_value());
    EXPECT_FALSE(book.CancelOrder(1).has_value());
}

TEST(CancelOrder, CancelFilledOrderReturnsNullopt) {
    OrderBook book;
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 10)); // fully fills order 1

    EXPECT_FALSE(book.CancelOrder(1).has_value());
}

// ── Trade Event Action ──

TEST(TradeEventAction, CallbackInvokedOnTrade) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 10));

    ASSERT_EQ(captured.size(), 1);
}

TEST(TradeEventAction, CallbackReceivesCorrectTradeFields) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 10));

    ASSERT_EQ(captured.size(), 1);
    EXPECT_EQ(captured[0].taker_order_id, 2);
    EXPECT_EQ(captured[0].maker_order_id, 1);
    EXPECT_EQ(captured[0].price, 100);
    EXPECT_EQ(captured[0].filled_quantity, 10);
    EXPECT_EQ(captured[0].sequence_number, 0);
    EXPECT_GT(captured[0].create_time, 0u);
}

TEST(TradeEventAction, NotInvokedWhenNoMatch) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 150, 10));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 10)); // no cross

    EXPECT_TRUE(captured.empty());
}

TEST(TradeEventAction, InvokedOncePerTradeAcrossMultipleLevels) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 101, 5));
    book.AddLimitOrder(MakeLimitSell(3, 102, 5));

    book.AddLimitOrder(MakeLimitBuy(4, 102, 15));

    ASSERT_EQ(captured.size(), 3);

    EXPECT_EQ(captured[0].price, 100);
    EXPECT_EQ(captured[0].maker_order_id, 1);
    EXPECT_EQ(captured[0].filled_quantity, 5);

    EXPECT_EQ(captured[1].price, 101);
    EXPECT_EQ(captured[1].maker_order_id, 2);
    EXPECT_EQ(captured[1].filled_quantity, 5);

    EXPECT_EQ(captured[2].price, 102);
    EXPECT_EQ(captured[2].maker_order_id, 3);
    EXPECT_EQ(captured[2].filled_quantity, 5);
}

TEST(TradeEventAction, SequenceNumbersIncrementAcrossCallbacks) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 101, 5));
    book.AddLimitOrder(MakeLimitBuy(3, 101, 10));

    ASSERT_EQ(captured.size(), 2);
    EXPECT_EQ(captured[0].sequence_number, 0);
    EXPECT_EQ(captured[1].sequence_number, 1);
}

TEST(TradeEventAction, MarketOrderTriggersCallback) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 10));
    book.AddMarketOrder(MakeMarketBuy(2, 10));

    ASSERT_EQ(captured.size(), 1);
    EXPECT_EQ(captured[0].taker_order_id, 2);
    EXPECT_EQ(captured[0].maker_order_id, 1);
    EXPECT_EQ(captured[0].price, 100);
    EXPECT_EQ(captured[0].filled_quantity, 10);
}

TEST(TradeEventAction, NoCallbackRegisteredDoesNotCrash) {
    OrderBook book;
    // no SetTradeEventAction call
    book.AddLimitOrder(MakeLimitSell(1, 100, 10));

    EXPECT_NO_THROW(book.AddLimitOrder(MakeLimitBuy(2, 100, 10)));
}

TEST(TradeEventAction, ReplacingCallbackUsesNewAction) {
    OrderBook book;
    std::vector<Trade> first;
    std::vector<Trade> second;

    book.SetTradeEventAction([&](const Trade& t) { first.push_back(t); });
    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(2, 100, 5));

    book.SetTradeEventAction([&](const Trade& t) { second.push_back(t); });
    book.AddLimitOrder(MakeLimitSell(3, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(4, 100, 5));

    EXPECT_EQ(first.size(), 1);
    EXPECT_EQ(second.size(), 1);
}

TEST(TradeEventAction, FIFOWithinLevelFiresCallbackPerFill) {
    OrderBook book;
    std::vector<Trade> captured;
    book.SetTradeEventAction([&](const Trade& t) { captured.push_back(t); });

    book.AddLimitOrder(MakeLimitSell(1, 100, 5));
    book.AddLimitOrder(MakeLimitSell(2, 100, 5));
    book.AddLimitOrder(MakeLimitBuy(3, 100, 10));

    ASSERT_EQ(captured.size(), 2);
    EXPECT_EQ(captured[0].maker_order_id, 1);
    EXPECT_EQ(captured[1].maker_order_id, 2);
    EXPECT_EQ(captured[0].taker_order_id, 3);
    EXPECT_EQ(captured[1].taker_order_id, 3);
}

} 
