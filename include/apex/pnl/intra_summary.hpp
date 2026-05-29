#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include "snapshot.hpp"

struct IntraSummary {
    int run;
    int seed;
    double final_total_pnl;
    int64_t final_inventory;
    double sharpe_ratio;
    double max_drawdown;
};

inline double getMaxDrawdown(const std::vector<PnLSnapshot>& snapshots) {
    auto max_pnl = 0.0;
    auto max_drawdown = 0.0;
    for (auto i = 0; i < snapshots.size(); ++i) {
        auto curr_pnl = snapshots[i].total_pnl;
        max_pnl = std::max(max_pnl, curr_pnl);
        max_drawdown = std::max(max_drawdown, max_pnl - curr_pnl);
    }
    return max_drawdown;
}

inline double getSharpeRatio(const std::vector<PnLSnapshot>& snapshots) {
    if (snapshots.size() <= 1) {
        return 0.0;
    }

    auto total_diff = 0.0;
    std::vector<double> pnl_diffs;
    pnl_diffs.reserve(snapshots.size() - 1);

    for (auto i = 1; i < snapshots.size(); ++i) {
        auto pnl_diff = snapshots[i].total_pnl - snapshots[i-1].total_pnl;
        pnl_diffs.push_back(pnl_diff);
    }

    for (const auto diff : pnl_diffs) {
        total_diff += diff;
    }
    auto mean = total_diff / pnl_diffs.size();

    auto total_deviations = 0.0;
    for (const auto diff : pnl_diffs) {
        total_deviations += (diff - mean) * (diff - mean);
    }
    auto variance = total_deviations / pnl_diffs.size();

    auto std_dev = std::sqrt(variance);
    return std_dev == 0.0 ? 0.0 : mean / std_dev;
}

inline IntraSummary buildIntraSummary(const std::vector<PnLSnapshot>& snapshots, int run, int seed) {
    auto empty = snapshots.empty();
    return IntraSummary{
        .run             = run,
        .seed            = seed,
        .final_total_pnl = empty ? 0.0 : snapshots.back().total_pnl,
        .final_inventory = empty ? 0   : snapshots.back().inventory,
        .sharpe_ratio    = getSharpeRatio(snapshots),
        .max_drawdown    = getMaxDrawdown(snapshots),
    };
}
