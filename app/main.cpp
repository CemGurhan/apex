#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <yaml-cpp/yaml.h>
#include "sim/html/html.hpp"
#include "sim/sim.hpp"
#include "sim/simcfg.hpp"

namespace {

constexpr const char* kSimConfigPath = "app/sim/simcfg.yaml";

MMStrategyConfig parseMMStrategy(YAML::Node node) {
    return MMStrategyConfig{
        .base_spread    = node["base_spread"].as<double>(),
        .skew_factor    = node["skew_factor"].as<double>(),
        .order_quantity = node["order_quantity"].as<double>(),
        .max_inventory  = node["max_inventory"].as<double>(),
    };
}

DistConfig parseDist(YAML::Node node) {
    DistConfig d{};
    if (!node) return d;
    if (auto n = node["sigma"])                 d.sigma                 = n.as<double>();
    if (auto n = node["fair_price_lambda"])     d.fair_price_lambda     = n.as<double>();
    if (auto n = node["arrive_rate_lambda"])    d.arrive_rate_lambda    = n.as<double>();
    if (auto n = node["order_qty_size_lambda"]) d.order_qty_size_lambda = n.as<double>();
    if (auto n = node["side_prob"])             d.side_prob             = n.as<double>();
    if (auto n = node["aggressive_prob"])       d.aggressive_prob       = n.as<double>();
    if (auto n = node["market_order_prob"])     d.market_order_prob     = n.as<double>();
    if (auto n = node["cancel_order_prob"])     d.cancel_order_prob     = n.as<double>();
    if (auto n = node["limit_order_prob"])      d.limit_order_prob      = n.as<double>();
    return d;
}

SimConfig loadSimConfig(const std::string& path) {
    auto node = YAML::LoadFile(path);
    return SimConfig{
        .tick_size          = node["tick_size"].as<double>(),
        .lot_size           = node["lot_size"].as<double>(),
        .duration           = std::chrono::seconds(node["duration_seconds"].as<int64_t>()),
        .pnl_csv_dir        = node["pnl_csv_dir"].as<std::string>(),
        .sim_run_iterations = node["sim_run_iterations"].as<int>(),
        .mm_strategy        = parseMMStrategy(node["mm_strategy"]),
        .dist               = parseDist(node["dist"]),
    };
}

void printUsage(std::string_view program) {
    std::cerr
        << "Usage: " << program << " <flag> [options]\n"
        << "\n"
        << "Flags:\n"
        << "  --sim                Run the simulated feeder against an orderbook + market maker.\n"
        << "  --clear              Remove every run_<uuid> subdir under the configured pnl_csv_dir.\n"
        << "  --html <run_dir>     Generate report.html from the intra/inter summary CSVs inside <run_dir>.\n"
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

    if (flag == "--html") {
        if (argc < 3) {
            std::cerr << "--html requires a run directory argument\n\n";
            printUsage(argv[0]);
            return 1;
        }
        std::string run_dir = argv[2];
        try {
            generateHtml(run_dir);
        } catch (const std::exception& e) {
            std::cerr << "failed to generate html report: " << e.what() << "\n";
            return 1;
        }
        std::cout << "Wrote: " << run_dir << "/report.html\n";
        return 0;
    }

    std::cerr << "unknown flag: " << flag << "\n\n";
    printUsage(argv[0]);
    return 1;
}
