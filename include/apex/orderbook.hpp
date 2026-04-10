#include <map>
#include <deque>
#include <functional>
#include "order.hpp"

class OrderBook {
    private:
        std::map<int64_t, std::deque<Order>, std::greater<int64_t>> bids; // have bids sort highest to lowest
        std::map<int64_t, std::deque<Order>> asks;

        template<typename MapType>
        void handleMarketOrder(Order& order, MapType& levels) {
            for (auto level_it = levels.begin(); level_it != levels.end();) {
                auto& resting_orders = level_it->second;
                auto is_order_filled = false;

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
                        is_order_filled = true;
                        break; // fully filled, nothing more to do.
                    } 
                }

                if (resting_orders.empty()) { // empty level, remove from book.
                    level_it = levels.erase(level_it);
                } else {
                    ++level_it;
                }

                if (is_order_filled) {
                    break;
                }
            }
        }

        template<typename MapType>
        void handleLimitOrder(Order& order, MapType& levels) {
            for ( auto level_it = levels.begin(); level_it != levels.end(); ) {
                auto level_price = level_it->first;
                auto& resting_orders = level_it->second;
                auto is_order_filled = false;

                 if (order.side == Side::Buy) {
                    if (level_price > order.price) {
                        break; // Rest of asks will be higher price - cannot exceed limit price on buy.
                    }
                } else {
                    if (level_price < order.price) {
                        break; // Rest of bids will be lower price - cannot go below limit price on sell. 
                    }
                }

                for (auto order_it = resting_orders.begin(); order_it != resting_orders.end(); ) {
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

                    if (resting_order.quantity == 0) {
                        order_it = resting_orders.erase(order_it);
                    } else {
                        ++order_it;
                    }

                    if (order.quantity == 0) {
                        is_order_filled = true;
                        break; // fully filled, nothing more to do.
                    } 
                }

                if (resting_orders.empty()) {
                    level_it = levels.erase(level_it); // clear level
                } else {
                    ++level_it;
                }

                if (is_order_filled) {
                    break; // fully filled, nothing more to do.
                }
            }

            if (order.quantity > 0 && order.side == Side::Buy) {
                // add remaining quantity to book.
                bids[order.price].push_back(order);
            } else if (order.quantity > 0 && order.side == Side::Sell) {
                // add remaining quantity to book.
                asks[order.price].push_back(order);
            }

            return;
        }
    
    public:
        Order AddLimitOrder(Order order) {
            if (order.type != OrderType::Limit) {
                throw std::invalid_argument("Order must be a limit order");
            }

            if (order.side == Side::Buy) {
                handleLimitOrder(order, asks);
                return order;
            }  

            handleLimitOrder(order, bids);
            return order;
        }

        Order AddMarketOrder(Order order) {
            if (order.type != OrderType::Market) {
                throw std::invalid_argument("Order must be a market order");
            }

            if (order.side == Side::Buy) {
                handleMarketOrder(order, asks);
                return order;
            }  

            handleMarketOrder(order, bids);
            return order;
        }

        int64_t GetBestBid() const {
            if (bids.empty()) {
                return 0;
            }

            // return highest val as best.
            return bids.begin()->first;
        }

        int64_t GetBestAsk() const {
            if (asks.empty()) {
                return 0;
            }

            // return lowest val as best.
            return asks.begin()->first;
        }
};