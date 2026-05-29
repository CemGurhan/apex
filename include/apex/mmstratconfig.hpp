#pragma once

#include <cstdint>

struct MMStrategyConfig {
    double base_spread;
    double skew_factor;
    uint64_t order_quantity;
    int64_t max_inventory;
};
