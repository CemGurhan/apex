#pragma once

#include "exchange.hpp"
#include <atomic>
#include <optional>
#include "orderbookclient.hpp"
#include "pnl/tracker.hpp"
#include "mmstratconfig.hpp"
#include "trade.hpp"

class MarketMaker {
    private:
        std::optional<uint64_t> active_bid_client_id;
        std::optional<uint64_t> active_ask_client_id;

        OrderBookClient& oBookClient;
        PnLTracker& pnlTracker;
        // Strategy knobs (base_spread, skew_factor, order_quantity, max_inventory).
        // tick_size is also in here but is unused by the MM itself — it lives in this
        // struct because it co-configures the venue the MM quotes on.
        MMStrategyConfig config;

        // inventory tracks the current net position of the MarketMaker. Positive means long,
        // negative means short.
        double inventory = 0;

        // client_id_counter is shared with any other producer hitting the same
        // orderbook (e.g. the simulated feeder). Each fetch_add yields a unique
        // client_id across producers, so no ID-space partitioning is needed.
        std::atomic<uint64_t>& client_id_counter;

        // getSpreadPrices returns the bid/ask price doubles to place around fair.
        // Returns {0, 0} when the book is empty.
        std::pair<double, double> getSpreadPrices();

        void cancelOrder(uint64_t client_id);
        void postLimitOrder(uint64_t client_id, double price, Side side);
        void placeQuotes();
        double marketMidPrice();

        public:
            MarketMaker(
                OrderBookClient& client,
                PnLTracker& pnlTracker,
                std::atomic<uint64_t>& client_id_counter,
                MMStrategyConfig config
            ) :
            oBookClient{client},
            pnlTracker{pnlTracker},
            config{config},
            client_id_counter{client_id_counter}
            {}

            void Start();

            double GetInventory() const { return inventory; }

            void TradeEventAction(const Trade& trade);
};
