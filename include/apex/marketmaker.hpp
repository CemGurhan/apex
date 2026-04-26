#include "exchange.hpp"
#include <functional>
#include <unordered_set>

class MarketMaker {
    private:
        Exchange& exchange;
        std::unordered_set<uint64_t> active_order_ids; 

        // base_spread is the minimum spread width. Can be widened or tightened 
        // depending on how the strategy is performing.
        uint64_t base_spread;
        // skew_factor dictates how aggressively to shift quotes.
        uint64_t skew_factor;
        // order_quantity is the size of each quote order placed by the MarketMaker.
        uint64_t order_quantity;
        // max_inventory is the maximum inventory the MarketMaker is willing to hold.
        uint64_t max_inventory;
};