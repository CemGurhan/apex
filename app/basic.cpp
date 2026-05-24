#include "basic.hpp"

#include <iostream>
#include <random>
#include <iomanip>
#include "apex/orderbook.hpp"
#include "apex/marketmaker.hpp"

namespace {

// seedBook populates the orderbook with some initial limit orders
// to create a market for the market maker to operate in.
void seedBook(OrderBook& book, int rng_seed, uint64_t seed_client_id) {
    std::mt19937 rng(rng_seed);
    std::uniform_int_distribution<uint64_t> mkt_qty_dist(1, 20);

    auto qty = mkt_qty_dist(rng);

    for (uint64_t price = 90; price <= 98; price++) {
        book.AddLimitOrder(Order{
            .client_id = seed_client_id++, .quantity = qty, .price = static_cast<double>(price),
            .side = Side::Buy, .type = OrderType::Limit
        });
    }
    for (uint64_t price = 102; price <= 110; price++) {
        book.AddLimitOrder(Order{
            .client_id = seed_client_id++, .quantity = qty, .price = static_cast<double>(price),
            .side = Side::Sell, .type = OrderType::Limit
        });
    }
}

void printColumnTitles() {
    std::cout << std::left
              << std::setw(5)  << "#"
              << std::setw(6)  << "Side"
              << std::setw(6)  << "Qty"
              << std::setw(8)  << "Filled"
              << std::setw(10) << "BestBid"
              << std::setw(10) << "BestAsk"
              << std::setw(10) << "Inv"
              << "Trades\n";
    std::cout << std::string(63, '-') << "\n";
}

void printResult(int round, Side side, uint64_t qty, const Order& result,
                  const OrderBook& book, const MarketMaker& mm, int trade_count) {
    std::cout << std::left
              << std::setw(5)  << round
              << std::setw(6)  << (side == Side::Buy ? "BUY" : "SELL")
              << std::setw(6)  << qty
              << std::setw(8)  << result.filled_quantity
              << std::setw(10) << book.GetBestBid()
              << std::setw(10) << book.GetBestAsk()
              << std::setw(10) << mm.GetInventory()
              << trade_count << "\n";
}

void runMarketMaker(OrderBook& book, int rng_seed, uint64_t seed_id) {
    // our overall spread between bid and ask offers.
    uint64_t spread = 1;
    // Factor that helps decide to how far from fair we should move.
    double skew_factor = 0.05;
    uint64_t order_qty = 10;
    int64_t max_inv = 50;

    std::mt19937 rng(rng_seed);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<uint64_t> mkt_qty_dist(1, 20);
    uint64_t order_client_id = 100000;

    printColumnTitles();

    int trade_count = 0;
    MarketMaker mm(book, spread, skew_factor, order_qty, max_inv);
    mm.Start();

    for (int round = 1; round <= 30; round++) {
        auto side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        auto qty = mkt_qty_dist(rng);

        auto result = book.AddMarketOrder(Order{
            .client_id = order_client_id++, .quantity = qty,
            .side = side, .type = OrderType::Market
        });

        if (result.filled_quantity > 0) trade_count++;

        printResult(round, side, qty, result, book, mm, trade_count);

        seed_id += 100;
        seedBook(book, ++rng_seed, seed_id); // add more liquidity to book after each round to keep the market going.
    }
}

}

void runBasic() {
    // tick size of trades.
    double tick_size = 1.0;
    OrderBook book = OrderBook(tick_size);

    auto rng_seed = 42;
    uint64_t seed_client_id = 500000;

    seedBook(book, ++rng_seed, seed_client_id);
    seed_client_id += 100;
    runMarketMaker(book, ++rng_seed, seed_client_id);
}
