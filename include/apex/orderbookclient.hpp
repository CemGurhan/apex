#include "orderbook.hpp"
#include "stream/ringbuffer.hpp"
#include "event.hpp"


class OrderBookClient {
    private:
        OrderBook& book;
        RingBuffer<OrderBookEvent, 1024>& event_buffer;

        void read() {

        }

    public:
        OrderBookClient(
            RingBuffer<OrderBookEvent, 1024>& buffer, 
            OrderBook& order_book
        ) : 
        event_buffer{buffer}, 
        book{order_book} 
        {
            
        }


};
