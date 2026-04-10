#include <map>
#include <deque>
#include <functional>
#include "order.hpp"
#include "price.hpp"

class OrderBook {
    private:
        std::map<Price, std::deque<Order>, std::greater<Price>> bids; // have bids sort highest to lowest
        std::map<Price, std::deque<Order>> asks;

        void handleMarketOrderBuy(Order&& order) {
            for (auto level_it = asks.begin(); level_it != asks.end();) {
                auto& resting_orders = level_it->second;

                for (auto order_it = resting_orders.begin(); order_it != resting_orders.end();) {
                    auto& resting_order = *order_it;

                    if (resting_order.quantity == 0) {
                        ++order_it;
                        continue; // shouldn't happen
                    }

                    if (resting_order.quantity >= order.quantity) {
                        resting_order.quantity -= order.quantity;
                        resting_order.filled_quantity += order.quantity;

                        order.filled_quantity += order.quantity;
                        order.quantity = 0; // fully filled
                    } else {
                        order.quantity -= resting_order.quantity;
                        order.filled_quantity += resting_order.quantity;

                        resting_order.filled_quantity += resting_order.quantity;
                        resting_order.quantity = 0; // fully filled
                    }

                    if (resting_order.quantity == 0) { // remove resting order from this level.
                        order_it = resting_orders.erase(order_it);
                    } else {
                        ++order_it;
                    }

                    if (order.quantity == 0) {
                        return; // fully filled, nothing more to do.
                    } 
                }

                if (resting_orders.empty()) { // empty level, remove from book.
                    level_it = asks.erase(level_it);
                } else {
                    ++level_it;
                }
            }
        }

        void handleBid(const Order& order) {
            

            return;
        }
    
    public:
        void AddOrder(const Order& order) {
            if (order.side == Side::Buy) {
                handleBid(order);
                return;
            }  

        }

        int64_t GetBestBid() const {
            if (bids.empty()) {
                return 0;
            }

            // return highest val as best.
            return bids.begin()->first.value;
        }

        int64_t GetBestAsk() const {
            if (asks.empty()) {
                return 0;
            }

            // return lowest val as best.
            return asks.begin()->first.value;
        }
};