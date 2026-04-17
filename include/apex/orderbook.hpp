#include <map>
#include <deque>
#include <functional>
#include "order.hpp"

struct PriceLevel {
    RestingOrder* head;
    RestingOrder* tail;
};

class OrderBook {
    private:
        std::map<int64_t, PriceLevel, std::greater<int64_t>> bids; // have bids sort highest to lowest
        std::map<int64_t, PriceLevel> asks;

        RestingOrder* convertOrderToRestingOrder(Order order) {
            return new RestingOrder{
                .id = order.id,
                .quantity = order.quantity,
                .filled_quantity = order.filled_quantity,
                .price = order.price,
                .side = order.side,
                .create_time = order.create_time,
                .type = order.type
            };
        }

        Order convertRestingOrderToOrder(RestingOrder* resting_order) {
            return Order{
                .id = resting_order->id,
                .quantity = resting_order->quantity,
                .filled_quantity = resting_order->filled_quantity,
                .price = resting_order->price,
                .side = resting_order->side,
                .create_time = resting_order->create_time,
                .type = resting_order->type
            };
        }

        // removeOrderFromLevel removes the given resting order from the given price level 
        // and returns the next order in the level.
        RestingOrder* removeOrderFromLevel(RestingOrder* resting_order, PriceLevel& price_level) {
            auto prev = resting_order->prev;
            auto next = resting_order->next;

            if (prev == nullptr) { // order we're removing was our head
                price_level.head = next;
            } else {
                prev->next = next; 
            }

            if (next == nullptr) {
                price_level.tail = prev; // order we're removing was our tail
            } else {
                next->prev = prev;
            }

            delete resting_order; // free memory of removed order.

            return next;
        }

        void matchOrder(RestingOrder& order, PriceLevel& price_level) {
            auto resting_orders = price_level.head;
            auto resting_order = resting_orders;

            while (resting_order != nullptr) {
                if (resting_order->quantity == 0) {
                    resting_order = resting_order->next;
                    continue; // shouldn't happen
                }

                if (resting_order->quantity >= order.quantity) {
                    resting_order->quantity -= order.quantity;
                    resting_order->filled_quantity += order.quantity;

                    order.filled_quantity += order.quantity;
                    order.quantity = 0; // fully filled
                } else {
                    order.quantity -= resting_order->quantity;
                    order.filled_quantity += resting_order->quantity;

                    resting_order->filled_quantity += resting_order->quantity;
                    resting_order->quantity = 0; // fully filled
                }

                if (resting_order->quantity == 0) { 
                    resting_order = removeOrderFromLevel(resting_order, price_level);
                } else {
                    resting_order = resting_order->next;
                }

                if (order.quantity == 0) {
                    break; // fully filled, nothing more to do.
                } 
            }

            return;
        }

        template<typename MapType>
        Order handleMarketOrder(Order order, MapType& levels) {
            auto* resting_order = convertOrderToRestingOrder(order);

            for (auto level_it = levels.begin(); level_it != levels.end();) {
                auto price_level = level_it->second;
                matchOrder(resting_order, price_level);

                if (price_level.head == nullptr) { // empty level, remove from book.
                    level_it = levels.erase(level_it);
                } else {
                    ++level_it;
                }

                if (resting_order.quantity == 0) {
                    break;
                }
            }

            return convertRestingOrderToOrder(resting_order);
        }

        void addOrder(RestingOrder& order, PriceLevel& price_level) {
            auto head = price_level.head;

            if (head != nullptr) {
                auto tail = price_level.tail;
                tail->next = &order;
                order.prev = tail;
                price_level.tail = &order; // update tail of this level.
            } else {
                price_level = {&order, &order};
            }
        }

        // validPrice returns true if the price is valid for this order.
        bool validPrice(int64_t order_price, int64_t price, Side side) {
            auto exceeds_buy_price = side == Side::Buy && price > order_price;
            auto below_sell_price = side == Side::Sell && price < order_price;

            if (exceeds_buy_price || below_sell_price) {
                return false; 
            }
            
            return true;
        }

        template<typename MapType>
        Order handleLimitOrder(Order limit_order, MapType& levels) {
            auto* order = convertOrderToRestingOrder(limit_order);

            for ( auto level_it = levels.begin(); level_it != levels.end(); ) {
                auto price = level_it->first;
                auto& price_level = level_it->second;

                if (!validPrice(order->price, price, order->side)) {
                    break; // can't match any more levels, stop.
                }

                matchOrder(order, price_level);

                if (price_level.head == nullptr) {
                    level_it = levels.erase(level_it); // clear level
                    continue;
                } 

                if (order.quantity == 0) {
                    break; // fully filled, nothing more to do.
                }

                ++level_it;
            }

            // add remaining quantity to bids side of book.
            if (order.quantity > 0 && limit_order.side == Side::Buy) {
                addOrder(order, bids[limit_order.price]);
            } 

            // add remaining quantity to asks side of book.
            if (order.quantity > 0 && limit_order.side == Side::Sell) {
                addOrder(order, asks[limit_order.price]);
            }

            return convertRestingOrderToOrder(order);
        }
    
    public:
        Order AddLimitOrder(Order order) {
            if (order.type != OrderType::Limit) {
                throw std::invalid_argument("Order must be a limit order");
            }

            if (order.side == Side::Buy) {
                return handleLimitOrder(order, asks);
            }  

            return handleLimitOrder(order, bids);
        }

        Order AddMarketOrder(Order order) {
            if (order.type != OrderType::Market) {
                throw std::invalid_argument("Order must be a market order");
            }

            if (order.side == Side::Buy) {
                return handleMarketOrder(order, asks);
            }  

            return handleMarketOrder(order, bids);
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