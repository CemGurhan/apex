#pragma once

#include <chrono>
#include <string>
#include "apex/dist_config.hpp"
#include "apex/mm_strategy_config.hpp"

struct SimConfig {
    double tick_size;
    std::chrono::seconds duration;
    std::string pnl_csv_dir;
    int sim_run_iterations;
    MMStrategyConfig mm_strategy;
    DistConfig dist;
};
