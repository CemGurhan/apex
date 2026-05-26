#pragma once

#include "simcfg.hpp"

// runSim wires up a simulated feeder against an orderbook and a market maker,
// then lets the feeder run for the configured duration before tearing
// everything down. Entry point for the --sim CLI flag.
void runSim(const SimConfig& cfg);
