#pragma once

#include <chrono>
#include <cstdint>
#include <string>

// SimConfig holds the runtime parameters for the simulation.
struct SimConfig {
    double tick_size;
    double base_spread;
    double skew_factor;
    uint64_t order_quantity;
    int64_t max_inventory;
    // duration is how long each independent simulation will run for.
    // E.g. if sim_run_iterations is 5 and duration is 10 seconds, the
    // total runtime will be about 50 seconds.
    std::chrono::seconds duration;
    // pnl_csv_dir is the directory each iteration's CSV is written into.
    // The file itself is named by wall-clock timestamp.
    std::string pnl_csv_dir;
    // sim_run_iterations is how many independent runs of runSim the monte carlo
    // performs back-to-back. Each iteration produces its own CSV.
    int sim_run_iterations;
};
