#include "sim.hpp"

#include <thread>
#include "apex/simfeeder.hpp"
#include "apex/marketmaker.hpp"
#include <iostream>

void runSim(std::chrono::seconds duration) {
    auto tick_size = 0.01;

    auto order_book = OrderBook(tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);

    auto feeder = SimulatedFeeder(client);
    feeder.Run();

    std::this_thread::sleep_for(std::chrono::seconds(1)); // let some orders flow in before starting market maker

    auto base_spread = 0.1;
    auto skew_factor = 0.01;
    auto order_quantity = 10;
    auto max_inventory = 100;
    auto market_maker = MarketMaker(client, base_spread, skew_factor, order_quantity, max_inventory);
    order_book.SetTradeEventAction([&market_maker](const Trade& trade) {
        market_maker.TradeEventAction(trade);
    });

    market_maker.Start();

    std::this_thread::sleep_for(duration);
    std::cout << "MM Strategy Inventory: " << market_maker.GetInventory() << "\n";
    
    // prevent stale events hitting trade event action on destroyed
    // market maker.
    client.Stop();
    feeder.Stop();
}
