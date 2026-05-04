#include <iostream>
#include <random>
#include <iomanip>
#include "apex/orderbook.hpp"
#include "apex/marketmaker.hpp"

// seedBook populates the orderbook with some initial limit orders 
// to create a market for the market maker to operate in.
void seedBook(OrderBook& book) {
    uint64_t seed_id = 50000;
    for (uint64_t price = 95; price <= 99; price++) {
        book.AddLimitOrder(Order{
            .id = seed_id++, .quantity = 50, .price = price,
            .side = Side::Buy, .type = OrderType::Limit
        });
    }
    for (uint64_t price = 101; price <= 105; price++) {
        book.AddLimitOrder(Order{
            .id = seed_id++, .quantity = 50, .price = price,
            .side = Side::Sell, .type = OrderType::Limit
        });
    }

    std::cout << "Book seeded: best_bid=" << book.GetBestBid()
              << " best_ask=" << book.GetBestAsk() << "\n\n";
}

void runMarketMaker(OrderBook& book) {
    uint64_t spread = 4;
    int64_t skew = 1;
    uint64_t order_qty = 10;
    int64_t max_inv = 50;
    double tick = 1.0;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<uint64_t> mkt_qty_dist(1, 20);
    uint64_t order_id = 100000;

    std::cout << std::left
              << std::setw(5)  << "#"
              << std::setw(6)  << "Side"
              << std::setw(6)  << "Qty"
              << std::setw(8)  << "Filled"
              << std::setw(10) << "BestBid"
              << std::setw(10) << "BestAsk"
              << std::setw(10) << "Inv"
              << "Trades\n";
    std::cout << std::string(55, '-') << "\n";

    int trade_count = 0;

    for (int round = 1; round <= 30; round++) {
        MarketMaker mm(book, spread, skew, order_qty, max_inv, tick);
        mm.Start();

        auto side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        auto qty = mkt_qty_dist(rng);

        auto result = book.AddMarketOrder(Order{
            .id = order_id++, .quantity = qty,
            .side = side, .type = OrderType::Market
        });

        if (result.filled_quantity > 0) trade_count++;

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
}

int main() {
    OrderBook book;
    seedBook(book);
    runMarketMaker(book);
    return 0;
}
