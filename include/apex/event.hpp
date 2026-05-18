#pragma once

#include "order.hpp"

enum class OrderBookEventType {
    MarketOrder,
    LimitOrder,
    CancelOrder,
};

// OrderBookEvent represents an event that can emitted
// to the order book.
struct OrderBookEvent {
    OrderBookEventType type;
    Order order;          
};
