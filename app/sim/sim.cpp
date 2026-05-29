#include "sim.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include "apex/simfeeder.hpp"
#include "apex/marketmaker.hpp"

namespace {

std::string generateUuid() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    auto hi = dist(rng);
    auto lo = dist(rng);

    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(8) << (hi >> 32)         << '-'
        << std::setw(4) << ((hi >> 16) & 0xFFFFULL) << '-'
        << std::setw(4) << (hi & 0xFFFFULL)   << '-'
        << std::setw(4) << (lo >> 48)         << '-'
        << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);
    return out.str();
}

std::string newRunDir(const std::string& parent) {
    auto dir = std::filesystem::path(parent) / ("run_" + generateUuid());
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::string pnlCsvPath(const std::string& dir, int iteration) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::ostringstream name;
    name << "pnl_" << iteration << "_"
         << std::put_time(&tm, "%Y%m%dT%H%M%S")
         << std::setw(3) << std::setfill('0') << ms.count()
         << ".csv";
    return (std::filesystem::path(dir) / name.str()).string();
}

}

void runSim(const SimConfig& cfg, int iteration, const std::string& runDir) {
    auto order_book = OrderBook(cfg.tick_size);
    auto buffer = RingBuffer<OrderBookEvent, 1024>();

    auto client = OrderBookClient(order_book, buffer);

    auto feeder = SimulatedFeeder(
        client,
        {1, 2048},
        100.0,
        DistConfig{},
        static_cast<uint64_t>(iteration)
    );
    feeder.Run();

    std::this_thread::sleep_for(std::chrono::seconds(1)); // let some orders flow in before starting market maker

    auto output_path = pnlCsvPath(runDir, iteration);
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

    pnl_tracker.WriteSnapshotsToCSV();
    

    // prevent stale events hitting trade event action on destroyed
    // market maker.
    client.Stop();
    feeder.Stop();
}

void startSimulation(const SimConfig& cfg) {
    std::filesystem::create_directories(cfg.pnl_csv_dir);
    auto run_dir = newRunDir(cfg.pnl_csv_dir);

    // NOTE: we use a mersene twister RNG in the sim feeder. Each iteration here
    // is used as a seed in the sim feeder's RNG. Thus, every iteration
    // starts off with similar outputs after expansion. As the sim feeder calls the
    // RNG the outputs between each iteration should start looking
    // very different. The longer we run the sim feeder the less the initial similarity 
    // will be of concern.
    // seed_seq could be used to apply noise to each seed if the sim feeder
    // is not random enough.
    for (int i = 1; i <= cfg.sim_run_iterations; ++i) {
        runSim(cfg, i, run_dir);
    }

    std::cout << "All simulations completed successfully. Outputs in: " << run_dir << "\n";
}

void clearRuns(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_directory() && entry.path().filename().string().starts_with("run_")) {
            std::filesystem::remove_all(entry.path());
        }
    }
}

