#pragma once

#include <cstdint>

struct IntraSummary {
    int run;
    int seed;
    double final_total_pnl;
    int64_t final_inventory;
    double sharpe_ratio;
    double max_drawdown;
};
