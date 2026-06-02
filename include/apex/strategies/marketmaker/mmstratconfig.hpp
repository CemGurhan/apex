#pragma once

struct MMStrategyConfig {
    // base_spread is the minimum spread between bid and ask prices.
    double base_spread;
    // skew_factor is used to adjust the reservation price based on inventory. 
    double skew_factor;
    // order_quantity is the quantity for each limit order we post.
    double order_quantity;
    // max_inventory is the maximum absolute inventory we want to hold. 
    // E.g. if max_inventory is 10, we don't want to be more than 10 long or 10 short.
    double max_inventory;
};
