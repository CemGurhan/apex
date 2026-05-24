#include "feeder.hpp"
#include <random>
#include <thread>
#include "side.hpp"
#include "orderbookclient.hpp"

class SimulatedFeeder : EventFeeder {
    private:
        OrderBookClient& client;

        // sigma controls how volatile our market is, controlling how wide our swings can be 
        // when selecting a random price under a normal distribution. Higher sigma means 
        // more volatile movements.
        // E.g. value of 2 indicates 2 sigma i.e. ~95% of values fall between 2 and -2,
        // anything out of that range is a 2 sigma event with a probability of 5% of 
        // occurring.
        double sigma = 0.01;

        // fair_price_lambda is used to determine how much our order prices cluster towards the fair
        // market price. A higher value indicates limit orders being placed closer to fair price.
        double fair_price_lambda = 10; 

        // arrival_rate_lambda is used to determine how frequently orders arrive in our market. 
        // A higher value indicates more frequent order arrivals.
        double arrive_rate_lambda = 10;

        // order_qty_size_lambda is used to determine how large our order quantities placed are.
        // A larger value means smaller order quantities.
        double order_qty_size_lambda = 10;

        // side_dist is used to determine the ratio of buys and sells
        // via a bernoulli distribution.
        // E.g. 0.5 indicates a 50% chance for either.
        double side_dist = 0.5; 

        // aggressive_dist is used to determine how often we place aggressive orders on the opposite
        // side of the book, rather than passive orders on the same side. 
        // E.g. 0.1 indicates that 10% of the time, we place an aggressive order.
        double aggressive_dist = 0.1; 

        std::pair<uint64_t, uint64_t> order_id_range = {1, 2048};

        std::atomic<uint64_t> client_id;

        double fair_price;

        void generateLimitOrder(
            std::exponential_distribution<double> offset_dist,
            std::bernoulli_distribution side_dist,
            std::normal_distribution<double> fair_price_dist,
            std::bernoulli_distribution aggressive,
            std::exponential_distribution<double> order_qty_size,
            std::mt19937 rng
        ) {
            // geometric brownian, not arithmetic, to keep +ve,
            // multiply fair price to get next val.
            fair_price *= std::exp(sigma * fair_price_dist(rng)); 
            auto side = side_dist(rng) ? Side::Buy : Side::Sell;

            auto price_offset = offset_dist(rng);
            double order_price = 0.0;
            if (side == Side::Buy) {
                order_price = aggressive(rng) ? fair_price + price_offset : fair_price - price_offset;
            } else {
                order_price = aggressive(rng) ? fair_price - price_offset : fair_price + price_offset;
            }
            generateOrderBookEvent(OrderBookEventType::LimitOrder, std::abs(order_price), side, order_qty_size, rng);
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

        OrderBookEvent generateOrderBookEvent(
            OrderBookEventType type, 
            double price, 
            Side side, 
            std::exponential_distribution<double> order_qty_size,
            std::mt19937 rng
        ) {
            return OrderBookEvent{
                .type = type,
                .order = Order{
                    .client_id = client_id.fetch_add(1),
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
            };
        }

        void seedRandomFairPrice(std::stop_token stop) {
            std::mt19937 rng(std::random_device{}()); // mersenne twister random number generator

            // We take a normal distribution here to follow the idea that prices move
            // with small independent steps - following brownian motion. A higher sigma
            // means more potential for mean divergence. 
            std::normal_distribution<double> fair_price_dist(0.0);

            // We take an exponential distribution to ensure that values are always positive,
            // and that larger values are rarer, when determining how close to fair we place
            // our orders.
            std::exponential_distribution<double> offset_dist(fair_price_lambda);

            // We take an exponential distribution to determine the time between order arrivals, 
            // ensuring that we have a positive time interval, and that longer intervals are rarer.
            std::exponential_distribution<double> arrival_dist(arrive_rate_lambda);

            // We take exponential distribution to determine the size of orders placed,
            // as most orders will be quite small, with larger orders placed more rarely.
            std::exponential_distribution<double> order_qty_size(order_qty_size_lambda);

            std::bernoulli_distribution side_dist(side_dist);
            std::bernoulli_distribution aggressive(aggressive_dist);


            while(!stop.stop_requested()) {
                generateLimitOrder(offset_dist, side_dist, fair_price_dist, aggressive, order_qty_size, rng);

                auto sleep_time = arrival_dist(rng);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sleep_time)));
            }
        }

    public:
        void Run() override {

        }

        void Stop() override {

        }

        SimulatedFeeder(
            OrderBookClient& client, 
            std::pair<int, int> order_id_range = {1, 2048},
            double starting_fair_price = 100.0
        ) : 
        client{client}, 
        client_id{order_id_range.first - 1},
        fair_price{starting_fair_price},
        order_id_range{order_id_range} {};
};
