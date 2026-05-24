#pragma once

#include <cstdint>
#include "side.hpp"

// OrderType represents the type of an order, 
// either Limit or Market.
enum class OrderType {
    Limit,
    Market
};

// Order represents an order.
struct Order {
    uint64_t id;
    uint64_t quantity;
    uint64_t filled_quantity = 0;
    double price;
    Side side;
    uint64_t create_time;
    OrderType type;
};
