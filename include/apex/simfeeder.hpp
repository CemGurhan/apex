#include "feeder.hpp"
#include <random>
#include <thread>
#include "side.hpp"
#include "orderbookclient.hpp"

class SimulatedFeeder : EventFeeder {
    private:
        OrderBookClient& client;

        // sigma controlshow volatile our market is, controlling how wide our swings can be 
        // when selecting a random price under a normal distribution. Higher sigma means 
        // more volatile movements.
        // E.g. value of 2 indicates 2 sigma i.e. ~95% of prices fall between 2 and -2,
        // anything out of that range is a 2 sigma event with a probability of 5% of 
        // occurring.
        int sigma = 10;

        // fair_price_lambda is used to determine how much our order prices cluster towards the fair
        // market price. A higher value indicates limit orders being placed closer to fair price.
        int fair_price_lambda = 10; 

        // arrival_rate_lambda is used to determine how frequently orders arrive in our market. 
        // A higher value indicates more frequent order arrivals.
        int arrive_rate_lambada = 10;

        OrderBookEvent generateOrderBookEvent(OrderBookEventType type, double price) {
            static std::atomic<uint64_t> order_id{0};
            return OrderBookEvent{
                .type = type,
                .order = Order{
                    .id = order_id.fetch_add(1),
                    .quantity = 100, // fixed quantity for now
                    .price = price,
                    .side = price > 100.0 ? Side::Sell : Side::Buy,
                    .create_time = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count()
                    ),
                    .type = type == OrderBookEventType::MarketOrder ? OrderType::Market : OrderType::Limit
                }   
            };
        }

        void seedRandomFairPrice(std::stop_token stop) {
            auto fair_price = 100.0;
            std::mt19937 rng(std::random_device{}()); // mersenne twister random number generator

            // We take a normal distribution here to follow the idea that prices move
            // with small independent steps - following brownian motion. A higher sigma
            // means more potential for mean divergence. 
            std::normal_distribution<double> price_step(0.0, sigma);

            // We take an exponential distribution to ensure that values are always positive,
            // and that larger values are rarer, when determining how close to fair we place
            // our orders.
            std::exponential_distribution<double> distance(fair_price_lambda);

            // We take an exponential distribution to determine the time between order arrivals, 
            // ensuring that we have a positive time interval, and that longer intervals are rarer.
            std::exponential_distribution<double> arrival(arrive_rate_lambada);

            while(!stop.stop_requested()) {
                fair_price += price_step(rng);
                auto price_offset = distance(rng);

                Side side;
                if (fair_price < 0) {
                    fair_price -= price_offset;
                    side = Side::Sell;
                } else {
                    fair_price += price_offset;
                    side = Side::Buy;
                }
                auto order_price = std::abs(fair_price);

                auto sleep_time = arrival(rng);
            }
        }

    public:
        void Run() override {

        }

        void Stop() override {

        }

        SimulatedFeeder(OrderBookClient& client) : client{client} {};
};
