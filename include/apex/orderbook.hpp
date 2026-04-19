#include <map>
#include <deque>
#include <functional>
#include "order.hpp"

// OrderNode represents an order resting at a 
// price level in the orderbook.
struct OrderNode {
    OrderNode* next = nullptr;
    OrderNode* prev = nullptr;
    int64_t id;
    int64_t quantity;
    int64_t filled_quantity = 0;
    int64_t price;
    Side side;
    uint64_t create_time;
    OrderType type;
};

struct PriceLevel {
    OrderNode* head = nullptr;
    OrderNode* tail = nullptr;
};

class OrderBook {
    private:
        std::map<int64_t, PriceLevel, std::greater<int64_t>> bids; // have bids sort highest to lowest
        std::map<int64_t, PriceLevel> asks;

        // convertOrderToOrderNode converts an order to an order node
        // that can be rested on the order book.
        OrderNode* convertOrderToOrderNode(Order order) {
            return new OrderNode{
                .id = order.id,
                .quantity = order.quantity,
                .filled_quantity = order.filled_quantity,
                .price = order.price,
                .side = order.side,
                .create_time = order.create_time,
                .type = order.type
            };
        }

        // removeOrderFromLevel removes the given order node from the given price level 
        // and returns the next order in the level.
        OrderNode* removeOrderFromLevel(OrderNode* order_node, PriceLevel& price_level) {
            auto prev = order_node->prev;
            auto next = order_node->next;

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

            delete order_node; // free memory of removed order.

            return next;
        }

        void matchOrder(Order& order, PriceLevel& price_level) {
            auto resting_order = price_level.head;

            while (resting_order != nullptr) {
                auto resting_order_qty = resting_order->quantity;

                if (resting_order_qty == 0) {
                    resting_order = resting_order->next;
                    continue; // shouldn't happen
                }

                // order can be filled with this resting order
                if (resting_order_qty >= order.quantity) {
                    resting_order->quantity -= order.quantity;
                    resting_order->filled_quantity += order.quantity;

                    order.filled_quantity += order.quantity;
                    order.quantity = 0; // fully filled
                } 
                
                // resting order can be filled with this order
                if (resting_order_qty <= order.quantity) {
                    order.quantity -= resting_order_qty;
                    order.filled_quantity += resting_order_qty;

                    resting_order->filled_quantity += resting_order_qty;
                    resting_order->quantity = 0; // fully filled
                }

                if (resting_order->quantity == 0) { 
                    resting_order = removeOrderFromLevel(resting_order, price_level);
                } else {
                    resting_order = resting_order->next;
                }

                if (order.quantity == 0) {
                    break; // order fully filled, nothing more to do.
                } 
            }

            return;
        }

        template<typename MapType>
        Order handleMarketOrder(Order& order, MapType& levels) {
            for (auto level_it = levels.begin(); level_it != levels.end();) {
                auto& price_level = level_it->second;
                matchOrder(order, price_level);

                if (price_level.head == nullptr) { // empty level, remove from book.
                    level_it = levels.erase(level_it);
                } else {
                    ++level_it;
                }

                if (order.quantity == 0) {
                    break;
                }
            }

            return order;
        }

        void addOrderToBook(const Order& o, PriceLevel& price_level) {
            auto* order = convertOrderToOrderNode(o);

            auto head = price_level.head;
            auto tail = price_level.tail;

            if ((head != nullptr && tail == nullptr) ||
                (head == nullptr && tail != nullptr)) {
                throw std::logic_error("Invalid price level state: head and tail should both be null or both be non-null");
            }

            if (head != nullptr) {
                tail->next = order;
                order->prev = tail;
                price_level.tail = order; // update tail of this level.
                return;
            } 
            
            price_level = {order, order};
        }

        // validPrice returns true if the price is valid for this order.
        bool validPrice(int64_t order_price, Side side, int64_t price) {
            auto exceeds_buy_price = side == Side::Buy && price > order_price;
            auto below_sell_price = side == Side::Sell && price < order_price;

            if (exceeds_buy_price || below_sell_price) {
                return false; 
            }
            
            return true;
        }

        template<typename MapType>
        Order handleLimitOrder(Order& order, MapType& levels) {
            for ( auto level_it = levels.begin(); level_it != levels.end(); ) {
                auto price = level_it->first;
                auto& price_level = level_it->second;

                if (!validPrice(order.price, order.side, price)) {
                    break; // can't match any more levels, stop.
                }

                matchOrder(order, price_level);

                if (price_level.head == nullptr) {
                    level_it = levels.erase(level_it); // clear level
                } else {
                    ++level_it;
                }

                if (order.quantity == 0) {
                    break; // fully filled, nothing more to do.
                }
            }

            if (order.quantity > 0 && order.side == Side::Buy) {
                // add remaining quantity to bids side of book.
                addOrderToBook(order, bids[order.price]);
            } 
            
            if (order.quantity > 0 && order.side == Side::Sell) {
                // add remaining quantity to asks side of book.
                addOrderToBook(order, asks[order.price]);
            } 

            return order;
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