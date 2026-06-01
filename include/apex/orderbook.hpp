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
#include <vector>

// OrderNode represents an order resting at a
// price level in the orderbook.
struct OrderNode {
    OrderNode* next = nullptr;
    OrderNode* prev = nullptr;
    // id is the orderbook's own internal identifier, assigned on rest.
    uint64_t id;
    // client_id is the identifier the submitting client set on the order.
    uint64_t client_id;
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
        std::atomic<uint64_t> cached_best_bid{0};
        std::atomic<uint64_t> cached_best_ask{0};
        std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids; // have bids sort highest to lowest
        std::map<uint64_t, PriceLevel> asks;
        std::deque<Trade> trades; // queue of trades processed in this book. Processed by background routine for post-trade.
        std::atomic<uint64_t> trade_sequence_number{0}; // sequence number for trades, incremented on each new trade.
        std::atomic<uint64_t> next_order_id{1};               // assigns each rested order a book-internal id
        std::unordered_map<uint64_t, OrderNode*> client_id_to_node; // keyed by client_id for cancel lookups
        std::vector<std::function<void(const Trade&)>> trade_event_actions;
        double tick_size = 0;
        double lot_size = 0;

        // toTicks normalizes a price (in price units) into the book's
        // internal integer tick representation.
        uint64_t toTicks(double price) const {
            return static_cast<uint64_t>(price / tick_size);
        }

        // toLots normalizes a quantity (in user units) into the book's
        // internal integer lot representation.
        uint64_t toLots(double quantity) const {
            return static_cast<uint64_t>(quantity / lot_size);
        }

        void fireTradeCallbacks(size_t from_index);
        void emitTrade(
            uint64_t fill_quantity,
            uint64_t price,
            uint64_t taker_client_id,
            uint64_t maker_client_id,
            Side side
        );
        void trade(Order& order, OrderNode* resting_order, uint64_t price);

        // convertOrderNodeToOrder converts an order node to an order that can be
        // returned to the caller.
        Order convertOrderNodeToOrder(const OrderNode* order_node);

        // convertOrderToOrderNode converts an order to an order node
        // that can be rested on the order book. The node's internal id is
        // assigned from the book's atomic counter; the order's client_id is
        // preserved on the node for client-facing lookups (cancel, trades).
        OrderNode* convertOrderToOrderNode(const Order& order);

        // removeOrderFromLevel removes the given order node from the given price level
        // and returns the next order in the level.
        OrderNode* removeOrderFromLevel(OrderNode* order_node, PriceLevel& price_level);

        void matchOrder(Order& order, PriceLevel& price_level, uint64_t price);

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

        void addOrderToBook(const Order& o, PriceLevel& price_level);

        // validPrice returns true if the price is valid for this order.
        bool validPrice(uint64_t order_price, Side side, uint64_t price);

        template<typename MapType>
        Order handleLimitOrder(Order& order, MapType& levels) {
            auto order_price_ticks = toTicks(order.price);

            for ( auto level_it = levels.begin(); level_it != levels.end(); ) {
                auto price = level_it->first;
                auto& price_level = level_it->second;

                if (!validPrice(order_price_ticks, order.side, price)) {
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
                addOrderToBook(order, bids[order_price_ticks]);
            }

            if (order.quantity > 0 && order.side == Side::Sell) {
                // add remaining quantity to asks side of book.
                addOrderToBook(order, asks[order_price_ticks]);
            }

            return order;
        }

        void refreshCache();

    public:
        OrderBook(double tick_size, double lot_size) : tick_size{tick_size}, lot_size{lot_size} {}

        Order AddLimitOrder(Order order) override;
        Order AddMarketOrder(Order order) override;

        // CancelOrder cancels the order with the given client_id and
        // returns the cancelled order. If the order was not found,
        // returns nullopt.
        std::optional<Order> CancelOrder(uint64_t client_id) override;

        double GetBestBid() override {
            // return highest val as best, denormalized to price units.
            return cached_best_bid.load(std::memory_order_acquire) * tick_size;
        }

        double GetBestAsk() override {
            // return lowest val as best, denormalized to price units.
            return cached_best_ask.load(std::memory_order_acquire) * tick_size;
        }

        // RegisterTradeEventAction registers an action to be taken on each trade event emitted by this order book.
        void RegisterTradeEventAction(std::function<void(const Trade&)> action) override {
            trade_event_actions.emplace_back(action);
        }

        double GetTickSize() const override {
            return tick_size;
        }

        double GetLotSize() const override {
            return lot_size;
        }
};
