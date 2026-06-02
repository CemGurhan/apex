#pragma once

#include <thread>
#include "apex/orderbook/orderbook.hpp"
#include "apex/stream/ringbuffer.hpp"
#include "apex/event.hpp"

// OrderBookClient enables interaction with the orderbook.
// It immediately begins running and processing once initialized.
class OrderBookClient {
    private:
        OrderBook& book;
        RingBuffer<OrderBookEvent, 1024>& event_buffer;
        int num_read_loops = 2048;
        std::jthread reader_thread;

        void orderbookAction(OrderBookEvent event);
        void read(std::stop_token stop);
        void start(std::stop_token stop);

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

        double GetTickSize() {
            return book.GetTickSize();
        }

        double GetLotSize() {
            return book.GetLotSize();
        }
};
