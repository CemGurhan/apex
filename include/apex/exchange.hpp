#pragma once

#include <functional>
#include "order.hpp"
#include "trade.hpp"

class Exchange {
public:
    virtual Order AddLimitOrder(Order order) = 0; // 0 means child must override this
    virtual Order AddMarketOrder(Order order) = 0;
    virtual std::optional<Order> CancelOrder(uint64_t order_id) = 0;
    virtual double GetBestBid() const = 0;
    virtual double GetBestAsk() const = 0;
    virtual void SetTradeEventAction(std::function<void(const Trade&)> action) = 0;
    virtual double GetTickSize() const = 0;
    virtual ~Exchange() = default;
};