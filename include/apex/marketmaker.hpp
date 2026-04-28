#include "exchange.hpp"
#include <functional>
#include <unordered_map>
#include <atomic>
#include <semaphore>

class MarketMaker {
    private:
        std::unordered_map<uint64_t, uint64_t> active_spread_ids;

        Exchange& exchange;
        // base_spread is the minimum spread width. Can be widened or tightened 
        // depending on how the strategy is performing.
        uint64_t base_spread;
        // skew_factor dictates how aggressively to shift quotes.
        uint64_t skew_factor;
        // order_quantity is the size of each quote order placed by the MarketMaker.
        uint64_t order_quantity;
        // max_inventory is the maximum inventory the MarketMaker is willing to hold.
        uint64_t max_inventory;

        // inventory tracks the current net position of the MarketMaker. Positive means long, 
        // negative means short.
        int64_t inventory = 0;

        double tick_size;

        uint64_t id = 0;

        // getSpreadPricesNormalized returns a pair of tick size normalized prices.
        // Pair 1 is the bid price, Pair 2 is the ask price.
        std::pair<uint64_t, uint64_t> getSpreadPricesNormalized() {
            auto best_bid = exchange.GetBestBid();
            auto best_ask = exchange.GetBestAsk();

            if (best_bid == 0 || best_ask == 0) {
                throw std::runtime_error("Cannot get spread prices: no bids or asks in the book");
            }

            auto fair = (best_ask + best_bid) / 2.0;
            fair = fair - (inventory * skew_factor); // shift quotes based on inventory position

            auto shift = base_spread / 2.0;
            auto bid_price = fair - shift;
            auto ask_price = fair + shift;

            auto bid_price_normalized = static_cast<uint64_t>(bid_price / tick_size);
            auto ask_price_normalized = static_cast<uint64_t>(ask_price / tick_size);
            
            return {bid_price_normalized, ask_price_normalized};
        }

        void placeQuotes() {
            auto prices = getSpreadPricesNormalized();
            auto bid_price_normalized = prices.first;
            auto ask_price_normalized = prices.second;

            auto id_bid = id++;
            auto id_ask = id++;

            active_spread_ids[id_bid] = id_ask;
            exchange.AddLimitOrder(Order{
                .id = id_bid,
                .quantity = order_quantity,
                .price = bid_price_normalized,
                .side = Side::Buy,
                .type = OrderType::Limit
            });

            
            active_spread_ids[id_ask] = id_bid;
            exchange.AddLimitOrder(Order{
                .id = id_ask,
                .quantity = order_quantity,
                .price = ask_price_normalized,
                .side = Side::Sell,
                .type = OrderType::Limit
            });
        }

        void tradeEventAction(const Trade& trade) {
            auto is_taker = false;
            uint64_t id_leg1 = 0;
            if (active_spread_ids.contains(trade.taker_order_id)) {
               id_leg1 = trade.taker_order_id;
               is_taker = true;
            } else if (active_spread_ids.contains(trade.maker_order_id)) {
               id_leg1 = trade.maker_order_id;
            } else {
                return; // trade doesn't involve one of our quotes, ignore
            }


            auto fill_quantity = trade.filled_quantity;
            auto side = trade.side;

            if (side == Side::Buy && is_taker || side == Side::Sell && !is_taker) {
                // either we bought or were sold to
                inventory += fill_quantity;
            } else if (side == Side::Sell && is_taker || side == Side::Buy && !is_taker) {
                // either we sold or were bought from
                inventory -= fill_quantity;
            }

            auto id_leg2 = active_spread_ids[id_leg1];
            try {
                exchange.CancelOrder(id_leg1);
            } catch (const std::invalid_argument& e) {
                // order was likely completely filled as is no longer resting, ignore error
            }
                   
            try {
                exchange.CancelOrder(id_leg2);
            } catch (const std::invalid_argument& e) {
                // order was likely completely filled as is no longer resting, ignore error
            }

            active_spread_ids.erase(id_leg1);
            active_spread_ids.erase(id_leg2);

            try {
                placeQuotes();
            } catch (const std::runtime_error& e) {
                // likely failed to place quotes due to empty book, return to caller.
            }
        }

        public:
            MarketMaker(
                Exchange& exchange,
                uint64_t base_spread,
                uint64_t skew_factor,
                uint64_t order_quantity,
                uint64_t max_inventory,
                double tick_size
            ) : 
            exchange{exchange}, 
            base_spread{base_spread}, 
            skew_factor{skew_factor}, 
            order_quantity{order_quantity}, 
            max_inventory{max_inventory}, 
            tick_size{tick_size}
            {
                exchange.SetTradeEventAction([this](const Trade& trade) {
                    this->tradeEventAction(trade);
                });
            }
        
            void Start() {
                placeQuotes(); // kickstart the strategy
            }
};
