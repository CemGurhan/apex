#include "exchange.hpp"
#include <functional>
#include <unordered_map>
#include <atomic>
#include <thread>

class MarketMaker {
    private:
        Exchange& exchange;
        std::unordered_map<uint64_t, uint64_t> active_order_ids; 

        // base_spread is the minimum spread width. Can be widened or tightened 
        // depending on how the strategy is performing.
        uint64_t base_spread;
        // skew_factor dictates how aggressively to shift quotes.
        uint64_t skew_factor;
        // order_quantity is the size of each quote order placed by the MarketMaker.
        uint64_t order_quantity;
        // max_inventory is the maximum inventory the MarketMaker is willing to hold.
        uint64_t max_inventory;

        double tick_size;

        std::atomic<uint64_t> id = 0;

        std::pair<uint64_t, uint64_t> getSpreadPricesNormalized() {
            auto best_bid = exchange.GetBestBid();
            auto best_ask = exchange.GetBestAsk();
            auto fair = best_ask + best_bid / 2.0;

            auto shift = base_spread / 2.0;
            auto bid_price = fair - shift;
            auto ask_price = fair + shift;

            auto bid_price_normalized = static_cast<uint64_t>(bid_price / tick_size);
            auto ask_price_normalized = static_cast<uint64_t>(ask_price / tick_size);
            
            return {bid_price_normalized, ask_price_normalized};
        }

        void tradeEventAction(const Trade& trade) {

        }

        public:
            MarketMaker(
                Exchange& exchange,
                uint64_t base_spread,
                uint64_t skew_factor,
                uint64_t order_quantity,
                uint64_t max_inventory,
                double tick_size
            ) : exchange{exchange} {
                this->base_spread = base_spread;
                this->skew_factor = skew_factor;
                this->order_quantity = order_quantity;
                this->max_inventory = max_inventory;
                this->tick_size = tick_size;

                exchange.SetTradeEventAction([this](const Trade& trade) {
                    this->tradeEventAction(trade);
                });
            }
        
            void Run(std::stop_token stop) {
                while (!stop.stop_requested()) {
                    auto prices = getSpreadPricesNormalized();
                    auto bid_price_normalized = prices.first;
                    auto ask_price_normalized = prices.second;

                    auto id_bid = id.fetch_add(1);
                    auto id_ask = id.fetch_add(1);

                    active_order_ids.insert(id_bid, id_ask);
                    exchange.AddLimitOrder(Order{
                        .id = id_bid,
                        .quantity = order_quantity,
                        .price = bid_price_normalized,
                        .side = Side::Buy,
                        .type = OrderType::Limit
                    });

                    
                    active_order_ids.insert(id_ask, id_bid);
                    exchange.AddLimitOrder(Order{
                        .id = id_ask,
                        .quantity = order_quantity,
                        .price = ask_price_normalized,
                        .side = Side::Sell,
                        .type = OrderType::Limit
                    });
                }
            }
};
