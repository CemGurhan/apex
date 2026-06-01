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
    // client_id is set by the submitting client.
    uint64_t client_id;
    double quantity;
    double filled_quantity = 0;
    double price;
    Side side;
    uint64_t create_time;
    OrderType type;
};
