#include "feeder.hpp"
#include <atomic>
#include <random>
#include <thread>
#include "side.hpp"
#include "orderbookclient.hpp"
#include "distconfig.hpp"

class SimulatedFeeder : EventFeeder {
    private:
        OrderBookClient& client;

        DistConfig config;

        // We take a normal distribution here to follow the idea that prices move
        // with small independent steps - following brownian motion. A higher sigma
        // means more potential for mean divergence.
        std::normal_distribution<double> fair_price_dist;

        // We take an exponential distribution to ensure that values are always positive,
        // and that larger values are rarer, when determining how close to fair we place
        // our orders.
        std::exponential_distribution<double> offset_dist;

        // We take an exponential distribution to determine the time between order arrivals,
        // ensuring that we have a positive time interval, and that longer intervals are rarer.
        std::exponential_distribution<double> arrival_dist;

        // We take exponential distribution to determine the size of orders placed,
        // as most orders will be quite small, with larger orders placed more rarely.
        std::exponential_distribution<double> order_qty_size;

        std::bernoulli_distribution side_dist;
        std::bernoulli_distribution aggressive_dist;

        // order_place_dist is a discrete distribution of different probabilities that
        // specific orders will be placed. 1 = market, 2 = limit, 3 = cancel.
        std::discrete_distribution<int> order_place_dist;

        std::mt19937 rng; // mersenne twister random number generator

        // active_order_ids tracks client IDs of resting limit orders we've placed,
        // so we can pick one at random when generating a cancel.
        std::vector<uint64_t> active_order_ids;

        std::atomic<uint64_t>& client_id_counter;

        double fair_price;

        std::jthread runner_thread;

        void generateMarketOrder() {
            auto side = side_dist(rng) ? Side::Buy : Side::Sell;
            pushOrderBookEvent(OrderBookEventType::MarketOrder, fair_price, side);
        }

        void generateLimitOrder() {
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

        OrderType getOrderTypeFromEventType(OrderBookEventType type) {
            switch (type) {
                case OrderBookEventType::MarketOrder:
                    return OrderType::Market;
                case OrderBookEventType::LimitOrder:
                    return OrderType::Limit;
                default:
                    throw std::invalid_argument("Invalid OrderBookEventType");
            }
        }

        void pushOrderBookEvent(
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
                    .quantity = static_cast<uint64_t>(order_qty_size(rng)) + 1,
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

        void generateCancelOrder() {
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
                    .quantity = 0,
                    .price = 0.0,
                    .side = Side::Buy,
                    .create_time = 0,
                    .type = OrderType::Limit
                }
            });
        }

        void run(std::stop_token stop) {
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

    public:
        void Run() override {
            runner_thread = std::jthread([this](std::stop_token stop) {run(stop);});
        }

        void Stop() override {
            runner_thread.request_stop();
        }

        SimulatedFeeder(
            OrderBookClient& client,
            std::atomic<uint64_t>& client_id_counter,
            double starting_fair_price = 100.0,
            DistConfig config = {},
            uint64_t seed = std::random_device{}()
        ) :
        client{client},
        config{config},
        fair_price_dist(0.0),
        offset_dist(config.fair_price_lambda),
        arrival_dist(config.arrive_rate_lambda),
        order_qty_size(config.order_qty_size_lambda),
        side_dist(config.side_prob),
        aggressive_dist(config.aggressive_prob),
        order_place_dist({config.market_order_prob, config.limit_order_prob, config.cancel_order_prob}),
        rng(seed),
        client_id_counter{client_id_counter},
        fair_price{starting_fair_price}
        {};
};
