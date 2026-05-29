#pragma once

#include <string>
#include "simcfg.hpp"

// runSim wires up a simulated feeder against an orderbook and a market maker,
// then lets the feeder run for the configured duration before tearing
// everything down.
void runSim(const SimConfig& cfg, int iteration, const std::string& runDir);

void startSimulation(const SimConfig& cfg);

void clearRuns(const std::string& dir);
