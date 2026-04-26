#include <functional>


struct Trade {
    uint64_t taker_order_id = 0;
    uint64_t maker_order_id = 0;
    uint64_t price = 0;
    uint64_t filled_quantity = 0;
    uint64_t create_time = 0;
    uint64_t sequence_number = 0;
};