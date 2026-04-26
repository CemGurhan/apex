#include "exchange.hpp"
#include <functional>
#include <unordered_set>

class MarketMaker {
    private:
        Exchange& exchange;
        int64_t inventory = 0;
        std::unordered_set<uint64_t> active_order_ids; 
        int64_t target_spread;
};