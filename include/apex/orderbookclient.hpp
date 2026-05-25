#pragma once

#include "orderbook.hpp"
#include "stream/ringbuffer.hpp"
#include "event.hpp"
#include <thread>

// OrderBookClient enables interaction with the orderbook.
// It immediately begins running and processing once initialized.
class OrderBookClient {
    private:
        OrderBook& book;
        RingBuffer<OrderBookEvent, 1024>& event_buffer;
        int num_read_loops = 2048;
        std::jthread reader_thread;

        void orderbookAction(OrderBookEvent event) {
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

        void read(std::stop_token stop) {
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

        void start(std::stop_token stop) {
            while (!stop.stop_requested()) {
                read(stop);
            }
        }

    public:
        OrderBookClient(
            OrderBook& order_book,
            RingBuffer<OrderBookEvent, 1024>& buffer
        ) : 
        event_buffer{buffer}, 
        book{order_book},
        reader_thread{std::jthread([this](std::stop_token stop) {start(stop);})}
        {}

        // Write writes an event to the client's buffer for processing.
        // If the clients buffer is full, this returns false. Otherwise,
        // returns true if successfully written.
        bool Write(OrderBookEvent&& event) {
            return event_buffer.Push(std::move(event));
        }

        // Stop stops the order book client.
        void Stop() {
            reader_thread.request_stop();
        }

        double GetBestBid() {
            return book.GetBestBid();
        }

        double GetBestAsk() {
            return book.GetBestAsk();
        }
};
