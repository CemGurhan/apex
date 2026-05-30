#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include "apex/pnl/intra_summary.hpp"

class Summary {
    private:
        const std::vector<IntraSummary>& summaries;
    
        void buildMedianPnL() {
            std::vector<double> pnls;
            for (const auto& s : summaries) pnls.push_back(s.final_total_pnl);

            auto mid = pnls.size() / 2;
            std::nth_element(pnls.begin(), pnls.begin() + mid, pnls.end());
            median_pnl = pnls[mid];
        }

        void buildMeanPnL() {
            for (const auto& s : summaries) {
                mean_pnl += s.final_total_pnl;
            }
            mean_pnl /= summaries.size();
        }

        void buildStdevPnL() {
            auto variance = 0.0;
            for (const auto& s : summaries) {
                variance += (s.final_total_pnl - mean_pnl) * (s.final_total_pnl - mean_pnl);
            }
            variance /= summaries.size();
            stdev_pnl = std::sqrt(variance);
        }

        void buildWinRate() {
            auto num_profitable = 0;
            for (const auto& s : summaries) {
                if (s.final_total_pnl > 0) {
                    num_profitable++;
                }
            }

            win_rate =  num_profitable / static_cast<double>(summaries.size());
        }

        void buildWorstAndBestMaxDrawdowns() {
            for (const auto& s : summaries) {
                best_max_drawdown = std::min(best_max_drawdown, s.max_drawdown);
                worst_max_drawdown = std::max(worst_max_drawdown, s.max_drawdown);
            }
        }

        void buildMedianSharpe() {
            std::vector<double> sharpe_ratios;
            for (const auto& s : summaries) sharpe_ratios.push_back(s.sharpe_ratio);

            auto mid = sharpe_ratios.size() / 2;
            std::nth_element(sharpe_ratios.begin(), sharpe_ratios.begin() + mid, sharpe_ratios.end());
            median_sharpe = sharpe_ratios[mid];
        }

        void buildMeanSharpe() {
            for (const auto& s : summaries) {
                mean_sharpe += s.sharpe_ratio;
            }
            mean_sharpe /= summaries.size();
        }
    public:
        double median_pnl = 0.0;
        double mean_pnl = 0.0;
        double stdev_pnl = 0.0;
        double win_rate = 0.0;
        double worst_max_drawdown = 0.0;
        double best_max_drawdown = std::numeric_limits<double>::max();
        double median_sharpe = 0.0;
        double mean_sharpe = 0.0;

        Summary(const std::vector<IntraSummary>& summaries) : summaries(summaries) {
            buildMedianPnL();
            buildMeanPnL();
            buildStdevPnL();
            buildWinRate();
            buildWorstAndBestMaxDrawdowns();
            buildMedianSharpe();
            buildMeanSharpe();
        }
};
