#include "apex/simfeeder/simfeeder.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

void SimulatedFeeder::generateMarketOrder() {
    auto side = side_dist(rng) ? Side::Buy : Side::Sell;
    pushOrderBookEvent(OrderBookEventType::MarketOrder, fair_price, side);
}

void SimulatedFeeder::generateLimitOrder() {
    auto side = side_dist(rng) ? Side::Buy : Side::Sell;

    auto price_offset = offset_dist(rng);
    double order_price = 0.0;
    if (side == Side::Buy) {
        order_price = aggressive_dist(rng) ? fair_price + price_offset : fair_price - price_offset;
    } else {
        order_price = aggressive_dist(rng) ? fair_price - price_offset : fair_price + price_offset;
    }

    if (order_price > 0) {
        pushOrderBookEvent(OrderBookEventType::LimitOrder, order_price, side);
    }
}

OrderType SimulatedFeeder::getOrderTypeFromEventType(OrderBookEventType type) {
    switch (type) {
        case OrderBookEventType::MarketOrder:
            return OrderType::Market;
        case OrderBookEventType::LimitOrder:
            return OrderType::Limit;
        default:
            throw std::invalid_argument("Invalid OrderBookEventType");
    }
}

void SimulatedFeeder::pushOrderBookEvent(
    OrderBookEventType type,
    double price,
    Side side
) {
    auto assigned_id = client_id_counter.fetch_add(1, std::memory_order_relaxed);
    if (type == OrderBookEventType::LimitOrder) {
        active_order_ids.push_back(assigned_id);
    }
    client.Write(OrderBookEvent{
        .type = type,
        .order = Order{
            .client_id = assigned_id,
            .quantity = order_qty_size(rng) + 1.0,
            .price = price,
            .side = side,
            .create_time = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            ),
            .type = getOrderTypeFromEventType(type)
        }
    });
}

void SimulatedFeeder::generateCancelOrder() {
    if (active_order_ids.empty()) {
        return;
    }

    std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids.size() - 1);
    auto idx = idx_dist(rng);
    auto cancel_id = active_order_ids[idx];

    active_order_ids[idx] = active_order_ids.back();
    active_order_ids.pop_back();

    client.Write(OrderBookEvent{
        .type = OrderBookEventType::CancelOrder,
        .order = Order{
            .client_id = cancel_id,
            .quantity = 0.0,
            .price = 0.0,
            .side = Side::Buy,
            .create_time = 0,
            .type = OrderType::Limit
        }
    });
}

void SimulatedFeeder::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        // geometric brownian, not arithmetic, to keep +ve,
        // multiply fair price to get next val.
        fair_price *= std::exp(config.sigma * fair_price_dist(rng));

        int order_type = order_place_dist(rng);

        if (order_type == 0) {
            generateMarketOrder();
        } else if (order_type == 1) {
            generateLimitOrder();
        } else {
            generateCancelOrder();
        }

        auto sleep_time = arrival_dist(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sleep_time)));
    }
}
