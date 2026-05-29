#pragma once

#include <functional>

// PnLSnapshot is a snapshot of PnL at a given 
// moment in time.
struct PnLSnapshot {
    uint64_t timestamp;
    // realized_pnl is our cash PnL
    double realized_pnl = 0;  
    // unrealized_pnl is the potential PnL, driven
    // from our current inventory and mid price of market.  
    double unrealized_pnl = 0; 
    // total_pnl is the total of realized and unrealized_pnl. 
    double total_pnl = 0;       
    int64_t inventory = 0;
};
