#include "apex/simfeeder.hpp"
#include "apex/marketmaker.hpp"

void runSim() {
    auto tick_size = 0.01;

    auto order_book = OrderBook(tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);
    auto feeder = SimulatedFeeder(client);
    feeder.Run();

    auto base_spread = 0.1;
    auto skew_factor = 0.01;
    auto order_quantity = 10;
    auto max_inventory = 100;
    auto market_maker = MarketMaker(client, base_spread, skew_factor, order_quantity, max_inventory);
    market_maker.Start();
}
