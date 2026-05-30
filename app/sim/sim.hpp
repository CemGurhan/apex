#pragma once

#include <string>
#include "simcfg.hpp"
#include "apex/pnl/intra_summary.hpp"

IntraSummary runSim(const SimConfig& cfg, int iteration, const std::string& runDir);

void startSimulation(const SimConfig& cfg);

void clearRuns(const std::string& dir);
