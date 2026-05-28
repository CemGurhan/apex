#pragma once

#include <cstdint>
#include <string>

// SimConfig holds the runtime parameters for the simulation.
struct SimConfig {
    double tick_size;
    double base_spread;
    double skew_factor;
    uint64_t order_quantity;
    int64_t max_inventory;
    std::string pnl_csv_path;
    // iterations is how many independent runs of runSim the monte carlo
    // performs back-to-back. Each iteration produces its own CSV.
    int iterations;
    // sim_iterations is the number of loop ticks the SimulatedFeeder performs
    // per runSim. 
    uint64_t sim_iterations;
};
