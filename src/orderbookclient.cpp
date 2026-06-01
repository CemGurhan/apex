#include "apex/orderbookclient.hpp"

void OrderBookClient::orderbookAction(OrderBookEvent event) {
    switch (event.type) {
        case OrderBookEventType::MarketOrder:
            book.AddMarketOrder(event.order);
            break;
        case OrderBookEventType::LimitOrder:
            book.AddLimitOrder(event.order);
            break;
        case OrderBookEventType::CancelOrder:
            book.CancelOrder(event.order.client_id);
            break;
    }
}

void OrderBookClient::read(std::stop_token stop) {
    auto found = false;
    auto event = OrderBookEvent{};

    for (auto i = 0; i < num_read_loops; ++i) {
        auto [e, ok] = event_buffer.Read();
        if (ok) {
            found = true;
            event = e;
            break;
        }
    }

    if (!found) {
        auto [e, ok] = event_buffer.TryRead(stop);
        if (ok) {
            found = true;
            event = e;
        }
    }

    if (!found) { // TryRead returns false if stop requested
        return;
    }

    orderbookAction(event);
}

void OrderBookClient::start(std::stop_token stop) {
    while (!stop.stop_requested()) {
        read(stop);
    }
}
