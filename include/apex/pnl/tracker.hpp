#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "../side.hpp"
#include "snapshot.hpp"
#include "intra_summary.hpp"

class PnLTracker {
    private:
        std::vector<PnLSnapshot> snapshots = {};
        std::string csv_write_path;
        std::string summary_csv_path;
        int run;
        int seed;
        std::optional<IntraSummary> intra_summary = std::nullopt;

        double getMaxDrawdown() const;
        double getSharpeRatio() const;
        void buildIntraSummary();
        void finaliseSummary();

    public:
        PnLTracker(std::string csv_write_path, std::string summary_csv_path, int run, int seed)
            : csv_write_path{csv_write_path},
              summary_csv_path{summary_csv_path},
              run{run},
              seed{seed} {}

        uint64_t now();

        void OnFill(
            Side side,
            double fill_qty,
            double fill_price,
            double mid_price,
            double inventory
        );

        // WriteRunResultsToCSV writes snapshots to the PnL store. It also
        // builds a sumamry of the run and writes this to a csv in the PnL store.
        void WriteRunResultsToCSV();

        // built, otherwise returns default and false.
        IntraSummary GetIntraSummary() const {
            if (!intra_summary.has_value()) {
                return IntraSummary{};
            }

            return intra_summary.value();
        }
};
