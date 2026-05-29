#pragma once

#include <string>
#include "simcfg.hpp"

void runSim(const SimConfig& cfg, int iteration, const std::string& runDir);

void startSimulation(const SimConfig& cfg);

void clearRuns(const std::string& dir);
