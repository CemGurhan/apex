#include "apex/marketmaker.hpp"

#include <iostream>
#include <stdexcept>

std::pair<double, double> MarketMaker::getSpreadPrices() {
    auto best_bid_price = oBookClient.GetBestBid();
    auto best_ask_price = oBookClient.GetBestAsk();

    if (best_bid_price == 0.0 && best_ask_price == 0.0) {
        return {0, 0}; // no bids or asks in the book
    }

    double fair;
    if (best_bid_price == 0.0) {
        fair = best_ask_price;
    } else if (best_ask_price == 0.0) {
        fair = best_bid_price;
    } else {
        fair = (best_ask_price + best_bid_price) / 2.0;
    }

    // shift quotes based on inventory position. E.g.
    // if inventory is negative, we're short, so our reservation price
    // is higher in an effort to be more long. Asks will be less likely
    // to fill (good - less shorting), bids will will be more likely to fill
    // (good - more long). reservation price is where we're indifferent
    // between buying and selling.
    auto reservation_price = fair - (inventory * config.skew_factor);

    auto shift = config.base_spread / 2.0;
    auto bid_price = reservation_price - shift;
    auto ask_price = reservation_price + shift;

    return {bid_price, ask_price};
}

void MarketMaker::cancelOrder(uint64_t client_id) {
    oBookClient.Write(OrderBookEvent{
        .type = OrderBookEventType::CancelOrder,
        .order = Order{
            .client_id = client_id
        }
    });
}

void MarketMaker::postLimitOrder(uint64_t client_id, double price, Side side) {
    oBookClient.Write(OrderBookEvent{
        .type = OrderBookEventType::LimitOrder,
        .order = Order{
            .client_id = client_id,
            .quantity = config.order_quantity,
            .price = price,
            .side = side,
            .type = OrderType::Limit
        }
    });
}

void MarketMaker::placeQuotes() {
    if (active_bid_client_id) cancelOrder(*active_bid_client_id);
    if (active_ask_client_id) cancelOrder(*active_ask_client_id);
    active_bid_client_id = std::nullopt;
    active_ask_client_id = std::nullopt;

    auto [bid_price, ask_price] = getSpreadPrices();
    auto order_qty_i64 = static_cast<int64_t>(config.order_quantity);

    auto too_long = inventory + order_qty_i64 > config.max_inventory;
    auto too_short = inventory - order_qty_i64 < -config.max_inventory;

    if (bid_price == 0.0 && ask_price == 0.0) {
        throw std::runtime_error("Cannot place quotes: no bids or asks in the book");
    }

    uint64_t bid_client_id = 0;
    uint64_t ask_client_id = 0;
    if (!too_long && !too_short) {
        bid_client_id = client_id_counter.fetch_add(1, std::memory_order_relaxed);
        ask_client_id = client_id_counter.fetch_add(1, std::memory_order_relaxed);
    } else if (!too_long) {
        bid_client_id = client_id_counter.fetch_add(1, std::memory_order_relaxed);
    } else if (!too_short) {
        ask_client_id = client_id_counter.fetch_add(1, std::memory_order_relaxed);
    }

    if (!too_long) {
        active_bid_client_id = bid_client_id;
        postLimitOrder(bid_client_id, bid_price, Side::Buy);
    }

    if (!too_short) {
        active_ask_client_id = ask_client_id;
        postLimitOrder(ask_client_id, ask_price, Side::Sell);
    }
}

double MarketMaker::marketMidPrice() {
    return (oBookClient.GetBestBid() + oBookClient.GetBestAsk()) / 2.0;
}

void MarketMaker::Start() {
    try {
        placeQuotes();
    } catch (const std::runtime_error& e) {
        std::cout << "MarketMaker caught an exception: " << e.what() << "\n";
    }
}

void MarketMaker::TradeEventAction(const Trade& trade) {
    auto is_bid_taker = active_bid_client_id.has_value() && trade.taker_client_id == active_bid_client_id.value();
    auto is_ask_taker = active_ask_client_id.has_value() && trade.taker_client_id == active_ask_client_id.value();

    auto is_bid_maker = active_bid_client_id.has_value() && trade.maker_client_id == active_bid_client_id.value();
    auto is_ask_maker = active_ask_client_id.has_value() && trade.maker_client_id == active_ask_client_id.value();

    uint64_t our_client_id = 0;
    if (is_ask_taker || is_bid_taker) {
        our_client_id = trade.taker_client_id;
    } else if (is_ask_maker || is_bid_maker) {
        our_client_id = trade.maker_client_id;
    } else {
        return; // trade doesn't involve one of our quotes, ignore
    }

    Side side = Side::Buy;
    auto fill_quantity = trade.filled_quantity;
    if (our_client_id == active_bid_client_id) { // we bought
        inventory += static_cast<int64_t>(fill_quantity);
    } else if (our_client_id == active_ask_client_id) { // we sold
        inventory -= static_cast<int64_t>(fill_quantity);
        side = Side::Sell;
    }

    pnlTracker.OnFill(
        side,
        fill_quantity,
        trade.price,
        marketMidPrice(),
        inventory,
        oBookClient.GetTickSize()
    );

    try {
        placeQuotes();
    } catch (const std::runtime_error& e) {
        std::cout << "MarketMaker caught an exception: " << e.what() << "\n";
    }
}
