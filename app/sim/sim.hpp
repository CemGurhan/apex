#pragma once

#include "simcfg.hpp"

// runSim wires up a simulated feeder against an orderbook and a market maker,
// then lets the feeder run for the configured duration before tearing
// everything down. Each call produces its own PnL CSV.
void runSim(const SimConfig& cfg);

// startSimulation runs runSim cfg.iterations times in a row. 
void startSimulation(const SimConfig& cfg);
