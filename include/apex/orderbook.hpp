#pragma once

#include <map>
#include <unordered_map>
#include <deque>
#include <functional>
#include "order.hpp"
#include <vector>
#include "trade.hpp"
#include <chrono>
#include <atomic>
#include "exchange.hpp"

// OrderNode represents an order resting at a 
// price level in the orderbook.
struct OrderNode {
    OrderNode* next = nullptr;
    OrderNode* prev = nullptr;
    uint64_t id;
    uint64_t quantity;
    uint64_t filled_quantity = 0;
    uint64_t price;
    Side side;
    uint64_t create_time;
    OrderType type;
};

// PriceLevel represents a price level in the orderbook,
// which is a linked list of orders resting at that price.
struct PriceLevel {
    OrderNode* head = nullptr;
    OrderNode* tail = nullptr;
};

class OrderBook : public Exchange {
    private:
        std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids; // have bids sort highest to lowest
        std::map<uint64_t, PriceLevel> asks;
        std::deque<Trade> trades; // queue of trades processed in this book. Processed by background routine for post-trade.
        std::atomic<uint64_t> trade_sequence_number{0}; // sequence number for trades, incremented on each new trade.
        std::unordered_map<uint64_t, OrderNode*> order_id_to_node; 
        std::function<void(const Trade&)> trade_event_action;
        double tick_size = 0;

        void fireTradeCallbacks(size_t from_index) {
            if (!trade_event_action) return;
            auto end = trades.size();
            for (size_t i = from_index; i < end; i++) {
                trade_event_action(trades[i]);
            }
        }

        void emitTrade(
            uint64_t fill_quantity,
            uint64_t price,
            uint64_t taker_order_id,
            uint64_t maker_order_id,
            Side side
        ) {
            if (fill_quantity == 0) {
                return; // no trade to emit
            }

            auto time_now = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );

            trades.emplace_back(
                Trade {
                    .taker_order_id = taker_order_id,
                    .maker_order_id = maker_order_id,
                    .price = price,
                    .filled_quantity = fill_quantity,
                    .create_time = time_now,
                    .sequence_number = trade_sequence_number.fetch_add(1),
                    .side = side
                }
            );
        }

        void trade(Order& order, OrderNode* resting_order, uint64_t price) {
            auto resting_order_qty = resting_order->quantity;
            auto order_qty = order.quantity;

            if (resting_order_qty >= order.quantity) {
                resting_order->quantity -= order.quantity;
                resting_order->filled_quantity += order.quantity;

                order.filled_quantity += order.quantity;
                order.quantity = 0; // fully filled

                emitTrade(order_qty, price, order.id, resting_order->id, order.side);
            } else if (resting_order_qty <= order.quantity) {
                order.quantity -= resting_order_qty;
                order.filled_quantity += resting_order_qty;

                resting_order->filled_quantity += resting_order_qty;
                resting_order->quantity = 0; // fully filled

                emitTrade(resting_order_qty, price, order.id, resting_order->id, order.side);
            }
        }

        // convertOrderNodeToOrder converts an order node to an order that can be 
        // returned to the caller.
        Order convertOrderNodeToOrder(const OrderNode* order_node) {
            return Order{
                .id = order_node->id,
                .quantity = order_node->quantity,
                .filled_quantity = order_node->filled_quantity,
                .price = order_node->price,
                .side = order_node->side,
                .create_time = order_node->create_time,
                .type = order_node->type
            };
        }

        // convertOrderToOrderNode converts an order to an order node
        // that can be rested on the order book.
        OrderNode* convertOrderToOrderNode(const Order& order) {
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
            order_id_to_node.erase(order_node->id);

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

        void matchOrder(Order& order, PriceLevel& price_level, uint64_t price) {
            auto resting_order = price_level.head;

            while (resting_order != nullptr) {
                auto resting_order_qty = resting_order->quantity;

                if (resting_order_qty == 0) {
                    resting_order = resting_order->next;
                    continue; // shouldn't happen
                }

                trade(order, resting_order, price);

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
                auto price = level_it->first;
                matchOrder(order, price_level, price);

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
                order_id_to_node[order->id] = order; 
                return;
            } 
            
            price_level = {order, order};
            order_id_to_node[order->id] = order; 
        }

        // validPrice returns true if the price is valid for this order.
        bool validPrice(uint64_t order_price, Side side, uint64_t price) {
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

                matchOrder(order, price_level, price);

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
        Order AddLimitOrder(Order order) override {
            if (order.type != OrderType::Limit) {
                throw std::invalid_argument("Order must be a limit order");
            }

            auto pre_trade_count = trades.size();

            if (order.side == Side::Buy) {
                order = handleLimitOrder(order, asks);
            } else {
                order = handleLimitOrder(order, bids);
            }

            fireTradeCallbacks(pre_trade_count);
            return order;
        }

        Order AddMarketOrder(Order order) override {
            if (order.type != OrderType::Market) {
                throw std::invalid_argument("Order must be a market order");
            }

            auto pre_trade_count = trades.size();

            if (order.side == Side::Buy) {
                order = handleMarketOrder(order, asks);
            } else {
                order = handleMarketOrder(order, bids);
            }

            fireTradeCallbacks(pre_trade_count);
            return order;
        }

        uint64_t GetBestBid() const override {
            if (bids.empty()) {
                return 0;
            }

            // return highest val as best.
            return bids.begin()->first;
        }

        uint64_t GetBestAsk() const override {
            if (asks.empty()) {
                return 0;
            }

            // return lowest val as best.
            return asks.begin()->first;
        }

        // CancelOrder cancels the order with the given order id and
        // returns the cancelled order. If the order was not found,
        // returns nullopt.
        std::optional<Order> CancelOrder(uint64_t order_id) override {
            auto it = order_id_to_node.find(order_id);

            if (it == order_id_to_node.end()) {
                return std::nullopt;
            }
            
            auto order_node = it->second;
            auto side = order_node->side;
            auto price = order_node->price;

            auto& price_level = side == Side::Buy ? bids[price] : asks[price];
            auto order_snap = convertOrderNodeToOrder(order_node);

            removeOrderFromLevel(order_node, price_level);

            if (price_level.head == nullptr) { // if level is empty after removing order, remove from book.
                if (side == Side::Buy) {
                    bids.erase(price);
                } else {
                    asks.erase(price);
                }
            }

            return order_snap;
        }

        // SetTradeEventAction sets the action to be taken on each trade event emitted by this order book.
        void SetTradeEventAction(std::function<void(const Trade&)> action) override {
            trade_event_action = action;
        }

        double GetTickSize() const {
            return tick_size;
        }

        OrderBook(double tick_size) : tick_size{tick_size} {}
};
