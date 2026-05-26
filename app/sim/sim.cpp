#include "sim.hpp"

#include <thread>
#include "apex/simfeeder.hpp"
#include "apex/marketmaker.hpp"
#include <iostream>

void runSim(const SimConfig& cfg) {
    auto order_book = OrderBook(cfg.tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);

    auto feeder = SimulatedFeeder(client);
    feeder.Run();

    std::this_thread::sleep_for(std::chrono::seconds(1)); // let some orders flow in before starting market maker

    auto market_maker = MarketMaker(client, cfg.base_spread, cfg.skew_factor, cfg.order_quantity, cfg.max_inventory);
    order_book.SetTradeEventAction([&market_maker](const Trade& trade) {
        market_maker.TradeEventAction(trade);
    });

    market_maker.Start();

    std::this_thread::sleep_for(cfg.duration);
    std::cout << "MM Strategy Inventory: " << market_maker.GetInventory() << "\n";

    // prevent stale events hitting trade event action on destroyed
    // market maker.
    client.Stop();
    feeder.Stop();
}
