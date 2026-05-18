#pragma once

#include <array>
#include <atomic>
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


// NOTES (remove if made to production):
// Modern CPUs have store buffers, out-of-order execution, and write combining. The processor sees 
// two independent stores — one to buffer[w] and one to write_idx — and might commit them to memory 
// in any order if it thinks it's faster. Your code says "write data then update index" but the CPU might 
// execute "update index then write data."

// On a single thread this is invisible — the CPU guarantees your own thread sees stores in program order. 
// But another thread reading those same memory locations might see the index update before the data write. 
// The consumer sees write_idx changed, reads the buffer slot, gets whatever garbage was there before 
// the producer's data arrived.

// memory_order_release tells the CPU: "flush everything I've written before this store. Don't let any earlier 
// writes be reordered past this point." It forces the data write to be visible before the index write, as seen 
// by other threads.

// When you use sync/atomic in Go, you're in the same territory as C++ atomics. atomic.Store and atomic.Load in Go 
// provide sequential consistency by default — the most expensive ordering, equivalent to C++'s seq_cst. Go doesn't 
// expose release/acquire as options. You get the strongest guarantee and pay the cost whether you need it or not.
// C++ gives you the choice. seq_cst if you want safety. release/acquire if you understand the ordering requirements 
// and want the performance. relaxed if you only need atomicity with no ordering at all (rare).

// seq_cst is sequential consistency

// Why reorder:

// The CPU is optimizing for single-threaded speed and doesn't know you have another thread watching.

// Concrete example: the producer writes data to `buffer[5]` (a cache line in main memory) then writes `write_idx = 6` 
// (a different cache line). If the cache line for `buffer[5]` is not in the CPU's L1 cache but `write_idx` is, the CPU faces 
// a choice: stall the entire pipeline waiting for `buffer[5]`'s cache line to load, or put the `buffer[5]` write into a store 
// buffer, skip ahead, and commit `write_idx` immediately since it's already in cache.

// The CPU picks option two — it's faster for single-threaded code and the result is identical from this thread's perspective. 
// The store buffer will flush `buffer[5]` eventually. But "eventually" might be after another core has already seen `write_idx = 6` 
// and tried to read `buffer[5]`.

// Other reasons CPUs reorder: write combining (batching multiple writes to adjacent memory into one bus transaction), speculative 
// execution (executing instructions before earlier ones complete), and store buffer forwarding (reads from your own recent writes 
// come from the store buffer, not memory).

// All of these are invisible to a single thread — the CPU guarantees you see your own operations in order. The problem only appears 
// when a second thread is observing the same memory. The CPU has no idea another core is watching. Memory ordering tells it to care.