#include "sim.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include "apex/simfeeder.hpp"
#include "apex/marketmaker.hpp"

namespace {

// Inserts a wall-clock timestamp suffix before the file extension so each run
// produces a uniquely-named output. e.g. "pnl/out.csv" -> "pnl/out_20260526T143045.csv".
std::string timestampedPath(const std::string& path) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm = *std::localtime(&now);

    std::ostringstream stamp;
    stamp << std::put_time(&tm, "%Y%m%dT%H%M%S");

    std::filesystem::path p(path);
    auto leaf = p.stem().string() + "_" + stamp.str() + p.extension().string();
    return (p.parent_path() / leaf).string();
}

}

void runSim(const SimConfig& cfg) {
    auto order_book = OrderBook(cfg.tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);

    auto feeder = SimulatedFeeder(client);
    feeder.Run();

    std::this_thread::sleep_for(std::chrono::seconds(1)); // let some orders flow in before starting market maker

    auto output_path = timestampedPath(cfg.pnl_csv_path);
    // make sure the parent dir exists so the ofstream open in WriteSnapshotsToCSV doesn't fail.
    std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
    auto pnl_tracker = PnLTracker(output_path);
    auto market_maker = MarketMaker(
        client, 
        pnl_tracker,
        cfg.base_spread, 
        cfg.skew_factor, 
        cfg.order_quantity, 
        cfg.max_inventory
    );

    order_book.RegisterTradeEventAction([&market_maker](const Trade& trade) {
        market_maker.TradeEventAction(trade);
    });

    market_maker.Start();

    std::this_thread::sleep_for(cfg.duration);
    std::cout << "Done: MM Strategy Inventory: " << market_maker.GetInventory() << "\n";
    
    std::cout << "Writing PnL snapshots to CSV at: " << output_path << "\n";
    pnl_tracker.WriteSnapshotsToCSV();
    std::cout << "PnL snapshots written to CSV successfully.\n";
    

    // prevent stale events hitting trade event action on destroyed
    // market maker.
    client.Stop();
    feeder.Stop();
}
