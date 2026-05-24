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
        int sigma = 10;

        // fair_price_lambda is used to determine how much our order prices cluster towards the fair
        // market price. A higher value indicates limit orders being placed closer to fair price.
        int fair_price_lambda = 10; 

        // arrival_rate_lambda is used to determine how frequently orders arrive in our market. 
        // A higher value indicates more frequent order arrivals.
        int arrive_rate_lambada = 10;

        std::pair<uint64_t, uint64_t> order_id_range = {1, 2048};

        std::atomic<uint64_t> client_id;

        double fair_price;

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

        OrderBookEvent generateOrderBookEvent(OrderBookEventType type, double price, Side side) {
            return OrderBookEvent{
                .type = type,
                .order = Order{
                    .client_id = client_id.fetch_add(1),
                    .quantity = 100, // fixed quantity for now
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
            auto fair_price_curr = fair_price;
            std::mt19937 rng(std::random_device{}()); // mersenne twister random number generator

            // We take a normal distribution here to follow the idea that prices move
            // with small independent steps - following brownian motion. A higher sigma
            // means more potential for mean divergence. 
            std::normal_distribution<double> price_step(0.0);

            // We take an exponential distribution to ensure that values are always positive,
            // and that larger values are rarer, when determining how close to fair we place
            // our orders.
            std::exponential_distribution<double> distance(fair_price_lambda);

            // We take an exponential distribution to determine the time between order arrivals, 
            // ensuring that we have a positive time interval, and that longer intervals are rarer.
            std::exponential_distribution<double> arrival(arrive_rate_lambada);

            while(!stop.stop_requested()) {
                // geometric brownian, not arithmetic, to keep +ve
                fair_price_curr = std::exp(sigma * price_step(rng)); 
                auto price_offset = distance(rng);

                Side side;
                double order_price = 0.0;
                if (fair_price_curr < fair_price) {
                    order_price = std::abs(fair_price_curr - price_offset);
                    side = Side::Sell;
                } else {
                    order_price = std::abs(fair_price_curr + price_offset);
                    side = Side::Buy;
                }
                fair_price = fair_price_curr;

                generateOrderBookEvent(OrderBookEventType::LimitOrder, order_price, side);

                auto sleep_time = arrival(rng);
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
