#pragma once

#include <functional>
#include "side.hpp"


struct Trade {
    uint64_t taker_order_id = 0; // initiator of trade
    uint64_t maker_order_id = 0; // resting order's ID
    uint64_t price = 0;
    uint64_t filled_quantity = 0;
    uint64_t create_time = 0;
    uint64_t sequence_number = 0;
    Side side; // side of the taker order
};