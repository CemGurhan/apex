#pragma once

#include <atomic>
#include <random>
#include <thread>
#include <vector>
#include "apex/feeder.hpp"
#include "apex/orderbookclient.hpp"
#include "apex/side.hpp"
#include "simfeederconfig.hpp"

class SimulatedFeeder : EventFeeder {
    private:
        OrderBookClient& client;

        SimFeederConfig config;

        // We take a normal distribution here to follow the idea that prices move
        // with small independent steps - following brownian motion. A higher sigma
        // means more potential for mean divergence.
        std::normal_distribution<double> fair_price_dist;

        // We take an exponential distribution to ensure that values are always positive,
        // and that larger values are rarer, when determining how close to fair we place
        // our orders.
        std::exponential_distribution<double> order_price_offset_dist;

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

        void generateMarketOrder();
        void generateLimitOrder();
        OrderType getOrderTypeFromEventType(OrderBookEventType type);
        void pushOrderBookEvent(OrderBookEventType type, double price, Side side);
        void generateCancelOrder();
        void run(std::stop_token stop);

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
            SimFeederConfig config = {},
            uint64_t seed = std::random_device{}()
        ) :
        client{client},
        config{config},
        fair_price_dist(0.0),
        order_price_offset_dist(config.order_price_lambda),
        arrival_dist(config.arrive_rate_lambda),
        order_qty_size(config.order_qty_size_lambda),
        side_dist(config.side_prob),
        aggressive_dist(config.aggressive_prob),
        order_place_dist({config.market_order_prob, config.limit_order_prob, config.cancel_order_prob}),
        rng(seed),
        client_id_counter{client_id_counter},
        fair_price{config.starting_fair_price}
        {};
};
