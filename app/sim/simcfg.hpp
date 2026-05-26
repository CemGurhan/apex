#pragma once

#include <chrono>
#include <cstdint>
#include <string>

// SimConfig holds the runtime parameters for runSim.
struct SimConfig {
    double tick_size;
    double base_spread;
    double skew_factor;
    uint64_t order_quantity;
    int64_t max_inventory;
    std::chrono::seconds duration;
    std::string pnl_csv_path;
};
