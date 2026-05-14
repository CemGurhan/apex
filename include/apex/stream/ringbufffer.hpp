#include <array>
#include <atomic>


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

        // toBufferIndex will ensure the idx given is always wrapped
        // within the bounds of the buffer. The mask is 1 less than capacity.
        // Given capacity is a multiple of 2, bitwise ANDs will wrap the index
        // around as needed.
        static uint64_t toBufferIndex(uint64_t idx) {
            return idx & mask;
        }

    public: 
        // RingBuffer should be static memory - do
        // not allow copying or moving resources to/from it.
        RingBuffer(const RingBuffer& other) = delete;
        RingBuffer& operator=(const RingBuffer& other) = delete;
        RingBuffer(RingBuffer&& other) = delete;
        RingBuffer& operator=(RingBuffer&& other) = delete;

        RingBuffer()  {
            static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
        }
        
        bool Push(T&& item) {
            // we can keep relaxed as this thread owns and writes to write_idx.
            auto w = write_idx.load(std::memory_order_relaxed);

            // acquiring the read_idx here ensures that the consumer thread has
            // already read from the buffer (no optimizations allow that to move
            // below this point).
            auto is_buffer_full = w - read_idx.load(std::memory_order_acquire) == Capacity;

            if (is_buffer_full) {
                return false;
            }

            buffer[toBufferIndex(w)] = std::move(item);
            // releasing here means our write to buffer is visible in the consumer before
            // it acquires the write_idx (no optimizations allow this to happen below this point).
            write_idx.store(w + 1, std::memory_order_release);
            
            return true;
        }

        std::pair<T, bool> Read() {
            // we can keep relaxed as this thread owns and writes to read_idx.
            auto r = read_idx.load(std::memory_order_relaxed);

            // acquiring the write_idx here ensures that the producer's thread 
            // write to the buffer is visible to us here (no optimization allows
            // those operations to move below this point).
            auto is_buffer_empty = r == write_idx.load(std::memory_order_acquire);

            if (is_buffer_empty) {
                return {T{}, false};
            }

            T item = buffer[toBufferIndex(r)];
            // the release here ensures that all our reads here are completed before the
            // producer's thread acquires the read_idx (no optimization
            // allows that to happen after this point).
            read_idx.store(r + 1, std::memory_order_release);

            return {item, true};
        }
};


// NOTES (remove if production moved):
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