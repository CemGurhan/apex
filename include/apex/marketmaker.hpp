#pragma once

#include "exchange.hpp"
#include <functional>


class MarketMaker {
    private:
        std::optional<uint64_t> active_bid_id;
        std::optional<uint64_t> active_ask_id;

        Exchange& exchange;
        // base_spread is the minimum spread width. Can be widened or tightened 
        // depending on how the strategy is performing.
        uint64_t base_spread;
        // skew_factor dictates how aggressively to shift quotes.
        int64_t skew_factor;
        // order_quantity is the size of each quote order placed by the MarketMaker.
        uint64_t order_quantity;
        // max_inventory is the maximum inventory the MarketMaker is willing to hold.
        int64_t max_inventory;

        // inventory tracks the current net position of the MarketMaker. Positive means long, 
        // negative means short.
        int64_t inventory = 0;

        double tick_size;

        uint64_t id = 1;

        // getSpreadPricesNormalized returns a pair of tick size normalized prices.
        // Pair 1 is the bid price, Pair 2 is the ask price.
        std::pair<uint64_t, uint64_t> getSpreadPricesNormalized() {
            auto best_bid = exchange.GetBestBid();
            auto best_ask = exchange.GetBestAsk();

            if (best_bid == 0 && best_ask == 0) {
                return {0, 0}; // no bids or asks in the book
            }

            double fair;
            if (best_bid == 0) {
                fair = static_cast<double>(best_ask);
            } else if (best_ask == 0) {
                fair = static_cast<double>(best_bid);
            } else {
                fair = (best_ask + best_bid) / 2.0;
            }

            // shift quotes based on inventory position. E.g.
            // if inventory is negative, we're short, so our fair value
            // is higher in an effort to be more long. Asks will be less likely
            // to fill (good - less shorting), bids will will be more likely to fill
            // (good - more long).
            fair = fair - (inventory * skew_factor); 

            auto shift = base_spread / 2.0;
            auto bid_price = fair - shift;
            auto ask_price = fair + shift;

            auto bid_price_normalized = static_cast<uint64_t>(bid_price / tick_size);
            auto ask_price_normalized = static_cast<uint64_t>(ask_price / tick_size);
            
            return {bid_price_normalized, ask_price_normalized};
        }

        void placeQuotes() {
            if (active_bid_id) exchange.CancelOrder(*active_bid_id);
            if (active_ask_id) exchange.CancelOrder(*active_ask_id);
            active_bid_id = std::nullopt;
            active_ask_id = std::nullopt;

            auto prices = getSpreadPricesNormalized();
            auto bid_price_normalized = prices.first;
            auto ask_price_normalized = prices.second;
            auto order_qty_i64 = static_cast<int64_t>(order_quantity);

            auto too_long = inventory + order_qty_i64 > max_inventory;
            auto too_short = inventory - order_qty_i64 < -max_inventory;

            if (ask_price_normalized == 0 && bid_price_normalized == 0) {
                throw std::runtime_error("Cannot place quotes: no bids or asks in the book");
            }

            // If no asks, we only continue if our inventory is too short i.e.
            // we need to be more long by placing a bid.
            if (ask_price_normalized == 0 && !too_short) {
                throw std::runtime_error("Cannot place quotes: no asks in the book");
            }

            // If no bids, we only continue if our inventory is too long i.e.
            // we need to be more short by placing an ask.
            if (bid_price_normalized == 0 && !too_long) {
                throw std::runtime_error("Cannot place quotes: no bids in the book");
            }

            uint64_t id_bid = 0;
            uint64_t id_ask = 0;
            if (!too_long && !too_short) {
                id_bid = id++;
                id_ask = id++;
            } else if (!too_long) {
                id_bid = id++;
            } else if (!too_short) {
                id_ask = id++;
            } 

            if (!too_long) {
                active_bid_id = id_bid;
                exchange.AddLimitOrder(Order{
                    .id = id_bid,
                    .quantity = order_quantity,
                    .price = bid_price_normalized,
                    .side = Side::Buy,
                    .type = OrderType::Limit
                });
            }

            if (!too_short) {   
                active_ask_id = id_ask;
                exchange.AddLimitOrder(Order{
                    .id = id_ask,
                    .quantity = order_quantity,
                    .price = ask_price_normalized,
                    .side = Side::Sell,
                    .type = OrderType::Limit
                });
            }        
        }

        void tradeEventAction(const Trade& trade) {
            auto is_bid_taker = active_bid_id.has_value() && trade.taker_order_id == active_bid_id.value();
            auto is_ask_taker = active_ask_id.has_value() && trade.taker_order_id == active_ask_id.value();

            auto is_bid_maker = active_bid_id.has_value() && trade.maker_order_id == active_bid_id.value();
            auto is_ask_maker = active_ask_id.has_value() && trade.maker_order_id == active_ask_id.value();

            uint64_t id_leg1 = 0;
            if (is_ask_taker || is_bid_taker) {
               id_leg1 = trade.taker_order_id;
            } else if (is_ask_maker || is_bid_maker) {                
                id_leg1 = trade.maker_order_id;
            } else {
                return; // trade doesn't involve one of our quotes, ignore
            }

            auto fill_quantity = trade.filled_quantity;
            if (id_leg1 == active_bid_id) { // we bought
                inventory += static_cast<int64_t>(fill_quantity);
            } else if (id_leg1 == active_ask_id) { // we sold
                inventory -= static_cast<int64_t>(fill_quantity);
            }   

            try {
                placeQuotes();
            } catch (const std::runtime_error& e) {
                // likely failed to place quotes due to empty book, return to caller.
                // TODO: log an error here. 
            }
        }

        public:
            MarketMaker(
                Exchange& exchange,
                uint64_t base_spread,
                int64_t skew_factor,
                uint64_t order_quantity,
                int64_t max_inventory,
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

            int64_t GetInventory() const { return inventory; }
};
