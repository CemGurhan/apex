#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>


template<typename T, uint64_t Capacity>
class RingBuffer {
    private:
        std::array<T, Capacity> buffer = {};
        std::atomic<uint64_t> read_idx = 0;
        std::atomic<uint64_t> write_idx = 0;
        // keep constexpr so we can load directly inline as constant
        // used for masking - compiler will optimize. Also don't need a new
        // bit of memory on every RingBuffer instance for this value.
        static constexpr uint64_t mask = Capacity - 1;

        std::mutex mutex = {};
        std::condition_variable cv = {};

        // toBufferIndex will ensure the idx given is always wrapped
        // within the bounds of the buffer. The mask is 1 less than capacity.
        // Given capacity is a multiple of 2, bitwise ANDs will wrap the index
        // around as needed.
        static uint64_t toBufferIndex(uint64_t idx) {
            return idx & mask;
        }

        void pushWithNotify(T&& item, uint64_t wrt_idx) {
            auto lock = std::scoped_lock(mutex);

            buffer[toBufferIndex(wrt_idx)] = std::move(item);
      
            write_idx.store(wrt_idx + 1, std::memory_order_release);
            cv.notify_one();
        }

        std::pair<T, bool> tryRead(std::stop_token stop) {
            auto lock = std::unique_lock<std::mutex>(mutex);

            // wake up cv if stop requested.
            std::stop_callback cb(stop, [this] { cv.notify_one(); });

            cv.wait(lock, [&] {
                return stop.stop_requested() ||
                read_idx.load(std::memory_order_relaxed) != 
                        write_idx.load(std::memory_order_acquire);
            });

            if (stop.stop_requested()) {
                return {T{}, false};
            }

            auto r = read_idx.load(std::memory_order_relaxed);

            T item = buffer[toBufferIndex(r)];
            read_idx.store(r + 1, std::memory_order_release);
            return {item, true};
        }

    public: 
        RingBuffer(const RingBuffer& other) = delete;
        RingBuffer& operator=(const RingBuffer& other) = delete;
        RingBuffer(RingBuffer&& other) = delete;
        RingBuffer& operator=(RingBuffer&& other) = delete;

        RingBuffer()  {
            static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
        }
        
        // Push pushes an item to the buffer. If the buffer is full it returns
        // false and the item is not pushed. Otherwise it returns true and the item is pushed.
        bool Push(T&& item) {
            auto w = write_idx.load(std::memory_order_relaxed);
            auto r = read_idx.load(std::memory_order_acquire);
            auto is_buffer_full = w - r == Capacity;

            if (is_buffer_full) {
                return false;
            }

            auto is_buffer_empty = r == w;
            if (is_buffer_empty) {
                // notify a push if buffer is empty. This way listeners waiting
                // for on the buffer can be told when it's active again.
                pushWithNotify(std::move(item), w);
                return true;
            }

            buffer[toBufferIndex(w)] = std::move(item);
            write_idx.store(w + 1, std::memory_order_release);

            return true;
        }

        // Read returns a pair of item from the buffer and a boolean. If the buffer
        // was empty the boolean will be false, otherwise true.
        std::pair<T, bool> Read() {
            auto r = read_idx.load(std::memory_order_relaxed);
            auto is_buffer_empty = r == write_idx.load(std::memory_order_acquire);

            if (is_buffer_empty) {
                return {T{}, false};
            }

            T item = buffer[toBufferIndex(r)];
            read_idx.store(r + 1, std::memory_order_release);

            return {item, true};
        }

        // TryRead is a blocking read operation, where it will block until an item
        // is available on the buffer. If the stop token was stopped, this returns false
        // and default. Otherwise it returns true and the item read.
        std::pair<T, bool> TryRead(std::stop_token stop) {
            auto r = read_idx.load(std::memory_order_relaxed);

            auto is_buffer_empty = r == write_idx.load(std::memory_order_acquire);

            if (is_buffer_empty) {
                return tryRead(stop);
            }

            auto item = buffer[toBufferIndex(r)];
            read_idx.store(r + 1, std::memory_order_release);
            return {item, true};
        }
};


// The CPU is optimizing for single-threaded speed and doesn't know you have another thread watching.

// Concrete example: the producer writes data to our ringbuffer `buffer[5]` (a cache line in main memory) then writes 
// `write_idx = 6`  (a different cache line). If the cache line for `buffer[5]` is not in the CPU's L1 cache but 
// `write_idx` is, the CPU faces a choice: stall the entire pipeline waiting for `buffer[5]`'s cache line to load, 
// or put the `buffer[5]` write into a STORE BUFFER (a small queue written to if the cache line isn't ready), 
// skip ahead, and commit `write_idx` immediately since it's already in cache.

// The CPU picks option two — it's faster for single-threaded code and the result is identical from this thread's perspective. 
// The store buffer will flush `buffer[5]` eventually. But "eventually" might be after another core has already seen 
// `write_idx = 6` and tried to read `buffer[5]`. The other core will see stale buffer data. E.g.

// 1. Core 1 writes buffer[5] = data → goes into core 1's store buffer
// 2. Core 1 writes write_idx = 6 → cache line is hot, goes straight to cache
// 3. Core 2 sees write_idx = 6 in cache → tries to read buffer[5]
// 4. Core 2 reads stale buffer[5] — core 1's write is still sitting in the store buffer, not yet flushed

// memory_order_release on the write_idx store forces the flush: "drain all pending writes from my store buffer 
// before this store becomes visible." Now buffer[5] is committed to cache before write_idx = 6 is. Core 2 sees 
// both in the correct order.
