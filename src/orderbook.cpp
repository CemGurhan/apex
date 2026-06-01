#include "apex/orderbook.hpp"

#include <stdexcept>

void OrderBook::fireTradeCallbacks(size_t from_index) {
    if (trade_event_actions.empty()) return;
    auto end = trades.size();
    for (size_t i = from_index; i < end; i++) {
        for (auto j = 0; j < trade_event_actions.size(); ++j) {
            trade_event_actions[j](trades[i]);
        }
    }
}

void OrderBook::emitTrade(
    uint64_t fill_quantity,
    uint64_t price,
    uint64_t taker_client_id,
    uint64_t maker_client_id,
    Side side
) {
    if (fill_quantity == 0) {
        return;
    }

    auto time_now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    trades.emplace_back(
        Trade {
            .taker_client_id = taker_client_id,
            .maker_client_id = maker_client_id,
            .price = static_cast<double>(price) * tick_size,
            .filled_quantity = static_cast<double>(fill_quantity) * lot_size,
            .create_time = time_now,
            .sequence_number = trade_sequence_number.fetch_add(1),
            .side = side
        }
    );
}

void OrderBook::trade(Order& order, OrderNode* resting_order, uint64_t price) {
    auto resting_order_qty = resting_order->quantity;
    auto order_qty = static_cast<uint64_t>(order.quantity);

    if (resting_order_qty >= order_qty) {
        resting_order->quantity -= order_qty;
        resting_order->filled_quantity += order_qty;

        order.filled_quantity += order.quantity;
        order.quantity = 0;

        emitTrade(order_qty, price, order.client_id, resting_order->client_id, order.side);
    } else if (resting_order_qty <= order_qty) {
        order.quantity -= resting_order_qty;
        order.filled_quantity += resting_order_qty;

        resting_order->filled_quantity += resting_order_qty;
        resting_order->quantity = 0;

        emitTrade(resting_order_qty, price, order.client_id, resting_order->client_id, order.side);
    }
}

Order OrderBook::convertOrderNodeToOrder(const OrderNode* order_node) {
    return Order{
        .client_id = order_node->client_id,
        .quantity = static_cast<double>(order_node->quantity) * lot_size,
        .filled_quantity = static_cast<double>(order_node->filled_quantity) * lot_size,
        .price = static_cast<double>(order_node->price) * tick_size,
        .side = order_node->side,
        .create_time = order_node->create_time,
        .type = order_node->type
    };
}

OrderNode* OrderBook::convertOrderToOrderNode(const Order& order) {
    return new OrderNode{
        .next = nullptr,
        .prev = nullptr,
        .id = next_order_id.fetch_add(1, std::memory_order_relaxed),
        .client_id = order.client_id,
        .quantity = static_cast<uint64_t>(order.quantity),
        .filled_quantity = static_cast<uint64_t>(order.filled_quantity),
        .price = toTicks(order.price),
        .side = order.side,
        .create_time = order.create_time,
        .type = order.type
    };
}

OrderNode* OrderBook::removeOrderFromLevel(OrderNode* order_node, PriceLevel& price_level) {
    client_id_to_node.erase(order_node->client_id);

    auto prev = order_node->prev;
    auto next = order_node->next;

    if (prev == nullptr) {
        price_level.head = next;
    } else {
        prev->next = next;
    }

    if (next == nullptr) {
        price_level.tail = prev;
    } else {
        next->prev = prev;
    }

    delete order_node;

    return next;
}

void OrderBook::matchOrder(Order& order, PriceLevel& price_level, uint64_t price) {
    auto resting_order = price_level.head;

    while (resting_order != nullptr) {
        auto resting_order_qty = resting_order->quantity;

        if (resting_order_qty == 0) {
            resting_order = resting_order->next;
            continue;
        }

        trade(order, resting_order, price);

        if (resting_order->quantity == 0) {
            resting_order = removeOrderFromLevel(resting_order, price_level);
        } else {
            resting_order = resting_order->next;
        }

        if (order.quantity == 0) {
            break;
        }
    }
}

void OrderBook::addOrderToBook(const Order& o, PriceLevel& price_level) {
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
        price_level.tail = order;
        client_id_to_node[order->client_id] = order;
        return;
    }

    price_level = {order, order};
    client_id_to_node[order->client_id] = order;
}

bool OrderBook::validPrice(uint64_t order_price, Side side, uint64_t price) {
    auto exceeds_buy_price = side == Side::Buy && price > order_price;
    auto below_sell_price = side == Side::Sell && price < order_price;

    if (exceeds_buy_price || below_sell_price) {
        return false;
    }

    return true;
}

void OrderBook::refreshCache() {
    cached_best_bid.store(
        bids.empty() ? 0 : bids.begin()->first,
        std::memory_order_release
    );
    cached_best_ask.store(
        asks.empty() ? 0 : asks.begin()->first,
        std::memory_order_release
    );
}

Order OrderBook::AddLimitOrder(Order order) {
    if (order.type != OrderType::Limit) {
        throw std::invalid_argument("Order must be a limit order");
    }

    auto pre_trade_count = trades.size();

    order.quantity = static_cast<double>(toLots(order.quantity));
    order.filled_quantity = static_cast<double>(toLots(order.filled_quantity));

    if (order.side == Side::Buy) {
        order = handleLimitOrder(order, asks);
    } else {
        order = handleLimitOrder(order, bids);
    }

    order.quantity *= lot_size;
    order.filled_quantity *= lot_size;

    refreshCache();
    fireTradeCallbacks(pre_trade_count);
    return order;
}

Order OrderBook::AddMarketOrder(Order order) {
    if (order.type != OrderType::Market) {
        throw std::invalid_argument("Order must be a market order");
    }

    auto pre_trade_count = trades.size();

    order.quantity = static_cast<double>(toLots(order.quantity));
    order.filled_quantity = static_cast<double>(toLots(order.filled_quantity));

    if (order.side == Side::Buy) {
        order = handleMarketOrder(order, asks);
    } else {
        order = handleMarketOrder(order, bids);
    }

    order.quantity *= lot_size;
    order.filled_quantity *= lot_size;

    refreshCache();
    fireTradeCallbacks(pre_trade_count);
    return order;
}

std::optional<Order> OrderBook::CancelOrder(uint64_t client_id) {
    auto it = client_id_to_node.find(client_id);

    if (it == client_id_to_node.end()) {
        return std::nullopt;
    }

    auto order_node = it->second;
    auto side = order_node->side;
    auto price = order_node->price;

    auto& price_level = side == Side::Buy ? bids[price] : asks[price];
    auto order_snap = convertOrderNodeToOrder(order_node);

    removeOrderFromLevel(order_node, price_level);

    if (price_level.head == nullptr) {
        if (side == Side::Buy) {
            bids.erase(price);
        } else {
            asks.erase(price);
        }
    }

    refreshCache();
    return order_snap;
}
