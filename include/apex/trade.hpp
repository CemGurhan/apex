#pragma once

#include <functional>
#include "side.hpp"


struct Trade {
    uint64_t taker_client_id = 0; // client ID of the trade initiator
    uint64_t maker_client_id = 0; // client ID of the resting order
    uint64_t price = 0;
    uint64_t filled_quantity = 0;
    uint64_t create_time = 0;
    uint64_t sequence_number = 0;
    Side side; // side of the taker order
};