#include "apex/pnl/tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

double PnLTracker::getMaxDrawdown() const {
    auto max_pnl = 0.0;
    auto max_drawdown = 0.0;
    for (auto i = 0; i < snapshots.size(); ++i) {
        auto curr_pnl = snapshots[i].total_pnl;
        max_pnl = std::max(max_pnl, curr_pnl);
        max_drawdown = std::max(max_drawdown, max_pnl - curr_pnl);
    }
    return max_drawdown;
}

double PnLTracker::getSharpeRatio() const {
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

void PnLTracker::buildIntraSummary() {
    auto empty = snapshots.empty();
    intra_summary = IntraSummary{
        .run             = run,
        .seed            = seed,
        .final_total_pnl = empty ? 0.0 : snapshots.back().total_pnl,
        .final_inventory = empty ? 0   : snapshots.back().inventory,
        .sharpe_ratio    = getSharpeRatio(),
        .max_drawdown    = getMaxDrawdown(),
    };
}

void PnLTracker::finaliseSummary() {
    buildIntraSummary();
    std::ofstream out(summary_csv_path, std::ios::app);
    if (out.tellp() == 0) {
        out << "run,seed,final_total_pnl,final_inventory,sharpe_ratio,max_drawdown\n";
    }
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << intra_summary->run << ',' << intra_summary->seed << ',' << intra_summary->final_total_pnl << ','
        << intra_summary->final_inventory << ',' << intra_summary->sharpe_ratio << ',' << intra_summary->max_drawdown << '\n';
}

uint64_t PnLTracker::now() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

void PnLTracker::OnFill(
    Side side,
    double fill_qty,
    double fill_price,
    double mid_price,
    double inventory
) {
    double cash_delta = fill_qty * fill_price;
    double new_realized = snapshots.empty() ? 0.0 : snapshots.back().realized_pnl;

    new_realized += (side == Side::Sell) ? cash_delta : -cash_delta;

    double unrealized = inventory * mid_price;

    snapshots.push_back({
        .realized_pnl = new_realized,
        .unrealized_pnl = unrealized,
        .total_pnl = new_realized + unrealized,
        .inventory = inventory,
        .timestamp = now(),
        });
}

void PnLTracker::WriteRunResultsToCSV() {
    std::ofstream file(csv_write_path);
    if (!file) {
        throw std::runtime_error("failed to open file for writing: " + csv_write_path);
    }

    file << std::setprecision(std::numeric_limits<double>::max_digits10);
    file << "timestamp,realized_pnl,unrealized_pnl,total_pnl,inventory\n";
    for (const auto& s : snapshots) {
        file << s.timestamp << ','
             << s.realized_pnl << ','
             << s.unrealized_pnl << ','
             << s.total_pnl << ','
             << s.inventory << '\n';
    }

    finaliseSummary();
}
