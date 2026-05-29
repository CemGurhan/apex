#include <vector>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include "snapshot.hpp"
#include "summary.hpp"

class PnLTracker {
    private:
        std::vector<PnLSnapshot> snapshots = {};
        std::string csv_write_path;
        std::string summary_csv_path;
        int run;
        int seed;

        // getMaxDrawdown gets the max drawdown observed across PnL
        // in our snapshots.
        double getMaxDrawdown() const {
            auto max_pnl = 0.0;
            auto max_drawdown = 0.0;
            for (auto i =0; i < snapshots.size(); ++i) {
                auto curr_pnl = snapshots[i].total_pnl;

                max_pnl = std::max(max_pnl, curr_pnl);
                max_drawdown = std::max(max_drawdown, max_pnl - curr_pnl);
            }

            return max_drawdown;
        }

        // getSharpeRatio gets the sharpe ratio of the PnL changes within
        // our current snapshots.
        double getSharpeRatio() const {
            if (snapshots.size() <= 1) {
                return 0.0; // Not enough data to calculate Sharpe ratio
            }

            auto sharpe_ratio = 0.0;
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
            sharpe_ratio = std_dev == 0.0 ? 0.0 : mean / std_dev;
            
            return sharpe_ratio;
        }

        void finaliseSummary() const {
            auto snapshot_empty = snapshots.empty();
            Summary s{
                .run             = run,
                .seed            = seed,
                .final_total_pnl = snapshot_empty ? 0.0 : snapshots.back().total_pnl,
                .final_inventory = snapshot_empty ? 0   : snapshots.back().inventory,
                .sharpe_ratio    = getSharpeRatio(),
                .max_drawdown    = getMaxDrawdown()
            };

            std::ofstream out(summary_csv_path, std::ios::app);
            out << std::setprecision(std::numeric_limits<double>::max_digits10)
                << s.run << ',' << s.seed << ',' << s.final_total_pnl << ','
                << s.final_inventory << ',' << s.sharpe_ratio << ',' << s.max_drawdown << '\n';
        }

    public:
        PnLTracker(std::string csv_write_path, std::string summary_csv_path, int run, int seed)
            : csv_write_path{csv_write_path},
              summary_csv_path{summary_csv_path},
              run{run},
              seed{seed} {}

        uint64_t now() {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
        }

        void OnFill(
            Side side,
            uint64_t fill_qty,
            uint64_t fill_price,
            double mid_price,
            int64_t inventory,
            double tick_size
        ) {
            auto f_price_denormalized = fill_price * tick_size;
            double cash_delta = fill_qty * f_price_denormalized;
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

        void WriteSnapshotsToCSV() const {
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
};
