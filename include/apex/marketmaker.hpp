#pragma once

#include "exchange.hpp"
#include <functional>
#include <iostream>
#include "orderbookclient.hpp"


class MarketMaker {
    private:
        std::optional<uint64_t> active_bid_client_id;
        std::optional<uint64_t> active_ask_client_id;

        OrderBookClient& oBookClient;
        // base_spread is the minimum spread width. Can be widened or tightened 
        // depending on how the strategy is performing.
        double base_spread;
        // skew_factor dictates how aggressively to shift quotes.
        double skew_factor;
        // order_quantity is the size of each quote order placed by the MarketMaker.
        uint64_t order_quantity;
        // max_inventory is the maximum inventory the MarketMaker is willing to hold.
        int64_t max_inventory;

        // inventory tracks the current net position of the MarketMaker. Positive means long, 
        // negative means short.
        int64_t inventory = 0;

        // client_id_counter generates unique client-side IDs for each quote
        // the market maker places. The orderbook tracks its own internal IDs.
        uint64_t client_id_counter = 1;

        // getSpreadPrices returns the bid/ask price doubles to place around fair.
        // Returns {0, 0} when the book is empty. 
        std::pair<double, double> getSpreadPrices() {
            auto best_bid_price = oBookClient.GetBestBid();
            auto best_ask_price = oBookClient.GetBestAsk();

            if (best_bid_price == 0.0 && best_ask_price == 0.0) {
                return {0, 0}; // no bids or asks in the book
            }

            double fair;
            if (best_bid_price == 0.0) {
                fair = best_ask_price;
            } else if (best_ask_price == 0.0) {
                fair = best_bid_price;
            } else {
                fair = (best_ask_price + best_bid_price) / 2.0;
            }

            // shift quotes based on inventory position. E.g.
            // if inventory is negative, we're short, so our reservation price
            // is higher in an effort to be more long. Asks will be less likely
            // to fill (good - less shorting), bids will will be more likely to fill
            // (good - more long). reservation price is where we're indifferent
            // between buying and selling.
            auto reservation_price = fair - (inventory * skew_factor);

            auto shift = base_spread / 2.0;
            auto bid_price = reservation_price - shift;
            auto ask_price = reservation_price + shift;

            return {bid_price, ask_price};
        }

        void cancelOrder(uint64_t client_id) {
            oBookClient.Write(OrderBookEvent{
                .type = OrderBookEventType::CancelOrder,
                .order = Order{
                    .client_id = client_id
                }
            });
        }

        void postLimitOrder(uint64_t client_id, double price, Side side) {
            oBookClient.Write(OrderBookEvent{
                .type = OrderBookEventType::LimitOrder,
                .order = Order{
                    .client_id = client_id,
                    .quantity = order_quantity,
                    .price = price,
                    .side = side,
                    .type = OrderType::Limit
                }
            });
        }

        void placeQuotes() {
            if (active_bid_client_id) cancelOrder(*active_bid_client_id);
            if (active_ask_client_id) cancelOrder(*active_ask_client_id);
            active_bid_client_id = std::nullopt;
            active_ask_client_id = std::nullopt;

            auto [bid_price, ask_price] = getSpreadPrices();
            auto order_qty_i64 = static_cast<int64_t>(order_quantity);

            auto too_long = inventory + order_qty_i64 > max_inventory;
            auto too_short = inventory - order_qty_i64 < -max_inventory;

            if (bid_price == 0.0 && ask_price == 0.0) {
                throw std::runtime_error("Cannot place quotes: no bids or asks in the book");
            }

            uint64_t bid_client_id = 0;
            uint64_t ask_client_id = 0;
            if (!too_long && !too_short) {
                bid_client_id = client_id_counter++;
                ask_client_id = client_id_counter++;
            } else if (!too_long) {
                bid_client_id = client_id_counter++;
            } else if (!too_short) {
                ask_client_id = client_id_counter++;
            }

            if (!too_long) {
                active_bid_client_id = bid_client_id;
                postLimitOrder(bid_client_id, bid_price, Side::Buy);
            }

            if (!too_short) {
                active_ask_client_id = ask_client_id;
                postLimitOrder(ask_client_id, ask_price, Side::Sell);
            }
        }

        public:
            MarketMaker(
                OrderBookClient& client,
                double base_spread,
                double skew_factor,
                uint64_t order_quantity,
                int64_t max_inventory
            ) : 
            oBookClient{client}, 
            base_spread{base_spread}, 
            skew_factor{skew_factor}, 
            order_quantity{order_quantity}, 
            max_inventory{max_inventory} 
            {}
        
            void Start() {
                try {
                    placeQuotes();
                } catch (const std::runtime_error& e) {
                    std::cout << "MarketMaker caught an exception: " << e.what() << "\n";
                }
            }

            int64_t GetInventory() const { return inventory; }

            void TradeEventAction(const Trade& trade) {
                auto is_bid_taker = active_bid_client_id.has_value() && trade.taker_client_id == active_bid_client_id.value();
                auto is_ask_taker = active_ask_client_id.has_value() && trade.taker_client_id == active_ask_client_id.value();

                auto is_bid_maker = active_bid_client_id.has_value() && trade.maker_client_id == active_bid_client_id.value();
                auto is_ask_maker = active_ask_client_id.has_value() && trade.maker_client_id == active_ask_client_id.value();

                uint64_t our_client_id = 0;
                if (is_ask_taker || is_bid_taker) {
                our_client_id = trade.taker_client_id;
                } else if (is_ask_maker || is_bid_maker) {
                    our_client_id = trade.maker_client_id;
                } else {
                    return; // trade doesn't involve one of our quotes, ignore
                }

                auto fill_quantity = trade.filled_quantity;
                if (our_client_id == active_bid_client_id) { // we bought
                    inventory += static_cast<int64_t>(fill_quantity);
                } else if (our_client_id == active_ask_client_id) { // we sold
                    inventory -= static_cast<int64_t>(fill_quantity);
                }

                try {
                    placeQuotes();
                } catch (const std::runtime_error& e) {
                    std::cout << "MarketMaker caught an exception: " << e.what() << "\n";
                }
            }
};
