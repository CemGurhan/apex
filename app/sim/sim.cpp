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
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::ostringstream stamp;
    stamp << std::put_time(&tm, "%Y%m%dT%H%M%S")
          << std::setw(3) << std::setfill('0') << ms.count();

    std::filesystem::path p(path);
    auto leaf = p.stem().string() + "_" + stamp.str() + p.extension().string();
    return (p.parent_path() / leaf).string();
}

}

void runSim(const SimConfig& cfg, int iteration) {
    auto order_book = OrderBook(cfg.tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);

    auto feeder = SimulatedFeeder(
        client,
        {1, 2048},
        100.0,
        DistConfig{},
        cfg.sim_feeder_iterations,
        static_cast<uint64_t>(iteration)
    );
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

    std::cout << "Done: MM Strategy Inventory: " << market_maker.GetInventory() << "\n";
    
    std::cout << "Writing PnL snapshots to CSV at: " << output_path << "\n";
    pnl_tracker.WriteSnapshotsToCSV();
    std::cout << "PnL snapshots written to CSV successfully.\n";
    

    // prevent stale events hitting trade event action on destroyed
    // market maker.
    client.Stop();
    feeder.Stop();
}

void startSimulation(const SimConfig& cfg) {
    // NOTE: we use a mersene twister RNG in the sim feeder. Each iteration here
    // is used as a seed in the sim feeder's RNG. Thus, every iteration
    // starts off with similar outputs after expansion. As the sim feeder calls the 
    // RNG the outputs between each iteration should start looking 
    // very different. We run the sim feeder for a large number of iterations
    // so the initial similarity shouldn't be a concern.
    // seed_seq could be used to apply noise to each seed if the sim feeder
    // is not random enough.
    for (int i = 1; i <= cfg.sim_run_iterations; ++i) {
        runSim(cfg, i);
    }
}
