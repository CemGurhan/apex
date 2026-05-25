#pragma once

#include <chrono>

// runSim wires up a simulated feeder against an orderbook and a market maker,
// then lets the feeder run for the given duration before tearing everything down.
// Entry point for the --sim CLI flag.
void runSim(std::chrono::seconds duration = std::chrono::seconds(30));
