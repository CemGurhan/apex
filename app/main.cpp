#include <iostream>
#include <optional>
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
        .tick_size          = node["tick_size"].as<double>(),
        .base_spread        = node["base_spread"].as<double>(),
        .skew_factor        = node["skew_factor"].as<double>(),
        .order_quantity     = node["order_quantity"].as<uint64_t>(),
        .max_inventory      = node["max_inventory"].as<int64_t>(),
        .duration           = std::chrono::seconds(node["duration_seconds"].as<int64_t>()),
        .pnl_csv_dir        = node["pnl_csv_dir"].as<std::string>(),
        .sim_run_iterations = node["sim_run_iterations"].as<int>(),
    };
}

void printUsage(std::string_view program) {
    std::cerr
        << "Usage: " << program << " <flag> [options]\n"
        << "\n"
        << "Flags:\n"
        << "  --sim                Run the simulated feeder against an orderbook + market maker.\n"
        << "  --clear              Remove every run_<uuid> subdir under the configured pnl_csv_dir.\n"
        << "\n"
        << "Options:\n"
        << "  --cfg <path>         Path to the sim YAML config. Defaults to " << kSimConfigPath << ".\n";
}

std::optional<std::string> parseCfgPath(int argc, char** argv) {
    std::string cfg_path = kSimConfigPath;
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--cfg") {
            if (i + 1 >= argc) {
                std::cerr << "--cfg requires a path argument\n\n";
                return std::nullopt;
            }
            cfg_path = argv[++i];
        } else {
            std::cerr << "unknown option: " << arg << "\n\n";
            return std::nullopt;
        }
    }
    return cfg_path;
}

std::optional<SimConfig> load(const std::string& path) {
    try {
        return loadSimConfig(path);
    } catch (const std::exception& e) {
        std::cerr << "failed to load sim config from '" << path << "': " << e.what() << "\n";
        return std::nullopt;
    }
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string_view flag = argv[1];

    if (flag == "--sim") {
        auto cfg_path = parseCfgPath(argc, argv);
        if (!cfg_path) {
            printUsage(argv[0]);
            return 1;
        }
        auto cfg = load(*cfg_path);
        if (!cfg) return 1;
        startSimulation(*cfg);
        return 0;
    }

    if (flag == "--clear") {
        auto cfg_path = parseCfgPath(argc, argv);
        if (!cfg_path) {
            printUsage(argv[0]);
            return 1;
        }
        auto cfg = load(*cfg_path);
        if (!cfg) return 1;
        clearRuns(cfg->pnl_csv_dir);
        std::cout << "Cleared all run_<uuid> subdirs under: " << cfg->pnl_csv_dir << "\n";
        return 0;
    }

    std::cerr << "unknown flag: " << flag << "\n\n";
    printUsage(argv[0]);
    return 1;
}
