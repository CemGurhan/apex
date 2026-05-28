#pragma once

#include "simcfg.hpp"

// runSim wires up a simulated feeder against an orderbook and a market maker,
// then lets the feeder run for the configured duration before tearing
// everything down. Each call produces its own PnL CSV. The iteration number
// is used to seed the feeder's RNG.
void runSim(const SimConfig& cfg, int iteration);

// startSimulation runs runSim cfg.iterations times in a row.
void startSimulation(const SimConfig& cfg);
