#include <vector>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include "snapshot.hpp"

class PnLTracker {
    private:
        std::vector<PnLSnapshot> snapshots = {};
        std::string csv_write_path;
    public:
        PnLTracker(std::string csv_write_path) : csv_write_path{csv_write_path} {}

        uint64_t now() {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
        }

        // OnFill process trade data into a snapshot on the PnLTracker.
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

        // WriteSnapshotsToCSV writes every snapshot recorded so far to the given
        // path.
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
        }
};
