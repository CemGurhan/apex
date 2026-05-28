#include <iostream>
#include <string>
#include <string_view>
#include <yaml-cpp/yaml.h>
#include "sim/sim.hpp"
#include "sim/simcfg.hpp"

namespace {

constexpr const char* kSimConfigPath = "app/sim/simcfg.yaml";

SimConfig loadSimConfig(const std::string& path) {
    auto node = YAML::LoadFile(path);
    return SimConfig{
        .tick_size      = node["tick_size"].as<double>(),
        .base_spread    = node["base_spread"].as<double>(),
        .skew_factor    = node["skew_factor"].as<double>(),
        .order_quantity = node["order_quantity"].as<uint64_t>(),
        .max_inventory  = node["max_inventory"].as<int64_t>(),
        .pnl_csv_path   = node["pnl_csv_path"].as<std::string>(),
        .sim_run_iterations    = node["sim_run_iterations"].as<int>(),
        .sim_feeder_iterations = node["sim_feeder_iterations"].as<uint64_t>(),
    };
}

void printUsage(std::string_view program) {
    std::cerr
        << "Usage: " << program << " <flag> [options]\n"
        << "\n"
        << "Flags:\n"
        << "  --sim                Run the simulated feeder against an orderbook + market maker.\n"
        << "\n"
        << "Options:\n"
        << "  --cfg <path>         Path to the sim YAML config. Defaults to " << kSimConfigPath << ".\n";
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string_view flag = argv[1];
    if (flag == "--sim") {
        std::string cfg_path = kSimConfigPath;

        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--cfg") {
                if (i + 1 >= argc) {
                    std::cerr << "--cfg requires a path argument\n\n";
                    printUsage(argv[0]);
                    return 1;
                }
                cfg_path = argv[++i];
            } else {
                std::cerr << "unknown option: " << arg << "\n\n";
                printUsage(argv[0]);
                return 1;
            }
        }

        SimConfig cfg;
        try {
            cfg = loadSimConfig(cfg_path);
        } catch (const std::exception& e) {
            std::cerr << "failed to load sim config from '" << cfg_path << "': " << e.what() << "\n";
            return 1;
        }
        startSimulation(cfg);
        return 0;
    }

    std::cerr << "unknown flag: " << flag << "\n\n";
    printUsage(argv[0]);
    return 1;
}
