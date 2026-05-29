#pragma once

#include <chrono>
#include <string>
#include "apex/distconfig.hpp"
#include "apex/mmstratconfig.hpp"

struct SimConfig {
    double tick_size;
    std::chrono::seconds duration;
    std::string pnl_csv_dir;
    int sim_run_iterations;
    MMStrategyConfig mm_strategy;
    DistConfig dist;
};
