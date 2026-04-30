#ifndef PROFILER_H
#define PROFILER_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>

namespace Profiler {

enum Bucket : int {
    MoveGeneration = 0,
    LegalityChecking,
    MakeMove,
    UndoMove,
    SlidingAttack,
    MemoryAllocation,
    HashState,
    BucketCount
};

struct Counter {
    uint64_t nanoseconds = 0;
    uint64_t calls = 0;
    uint64_t bytes = 0;
};

inline bool enabled = false;
inline bool countAllocations = false;
inline std::array<Counter, BucketCount> counters{};
inline std::atomic_uint64_t allocationCalls{ 0 };
inline std::atomic_uint64_t allocationBytes{ 0 };
inline std::atomic_uint64_t allocationNanoseconds{ 0 };

inline void reset() {
    for (Counter& counter : counters) {
        counter = Counter{};
    }
    allocationCalls.store(0, std::memory_order_relaxed);
    allocationBytes.store(0, std::memory_order_relaxed);
    allocationNanoseconds.store(0, std::memory_order_relaxed);
}

inline void add(Bucket bucket, uint64_t nanoseconds) {
    Counter& counter = counters[static_cast<int>(bucket)];
    counter.nanoseconds += nanoseconds;
    ++counter.calls;
}

inline void recordAllocation(std::size_t bytes, uint64_t nanoseconds) {
    if (!countAllocations) {
        return;
    }
    allocationCalls.fetch_add(1, std::memory_order_relaxed);
    allocationBytes.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
    allocationNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);

    if (!enabled) {
        return;
    }
    Counter& counter = counters[MemoryAllocation];
    ++counter.calls;
    counter.bytes += static_cast<uint64_t>(bytes);
    counter.nanoseconds += nanoseconds;
}

class ScopedTimer {
public:
    explicit ScopedTimer(Bucket bucket)
        : bucket(bucket), active(enabled) {
        if (active) {
            start = Clock::now();
        }
    }

    ~ScopedTimer() {
        if (!active) {
            return;
        }
        auto end = Clock::now();
        add(bucket, static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }

private:
    using Clock = std::chrono::high_resolution_clock;

    Bucket bucket;
    bool active;
    Clock::time_point start{};
};

} // namespace Profiler

#endif // PROFILER_H
