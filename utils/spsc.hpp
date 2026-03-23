#include <atomic>
#include <bits/stdc++.h>
#include "types.hpp"
template <typename T, size_t Capacity>
class SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "capacoty must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) T buffer_[Capacity];

public:
    FORCE_INLINE bool push(const T& item) noexcept {
    const size_t h = head_.load(std::memory_order_relaxed);
    const size_t t = tail_.load(std::memory_order_acquire);

    // queue full
    if ((h - t) == Capacity - 1) {
        return false;
    }

    buffer_[h & MASK] = item;

    head_.store(h + 1, std::memory_order_release);
    return true;
}
   
    FORCE_INLINE bool pop(T& item) noexcept {
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t h = head_.load(std::memory_order_acquire);

    // queue empty
    if (t == h) {
        return false;
    }

    item = buffer_[t & MASK];

    tail_.store(t + 1, std::memory_order_release);
    return true;
}

    [[nodiscard]] FORCE_INLINE bool empty() const noexcept{
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    
    
};
