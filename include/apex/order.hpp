#include <cstdint>
#include "side.hpp"

// OrderType represents the type of an order, 
// either Limit or Market.
enum class OrderType {
    Limit,
    Market
};

// Order represents an order in the orderbook.
struct Order {
    int64_t id;
    int64_t quantity;
    int64_t filled_quantity;
    int64_t price;
    Side side;
    uint64_t create_time;
    OrderType type;
};
