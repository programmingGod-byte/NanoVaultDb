#pragma once

#include "hft.hpp"
#include "utils/types.hpp"
#include <cassert>
#include <cstddef>
#include <utility>
#include <new> 
#include "strategyinclude.hpp"

template<typename T, std::size_t N>
struct alignas(CACHELINE) StrategyPool {
    std::aligned_storage_t<sizeof(T), alignof(T)> storage[N];
    std::size_t next = 0;

    template<typename... Args>
    T* acquire(Args&&... args) {
        if (next >= N) [[unlikely]] {
            std::abort();
        }

        void* ptr = &storage[next];
        T* slot = reinterpret_cast<T*>(ptr);

        T* obj = new (slot) T(std::forward<Args>(args)...);
        ++next;
        return obj;
    }

    void release_all() {
        for (std::size_t i = 0; i < next; i++) {
            T* obj = std::launder(reinterpret_cast<T*>(&storage[i]));
            obj->~T();
        }
        next = 0;
    }
};
static StrategyPool<AGAIN, HFT::MAXHFTSYMBOL> againPool;
static StrategyPool<BASIC, HFT::MAXHFTSYMBOL> basicPool;
static StrategyPool<BASIC3, HFT::MAXHFTSYMBOL> basicPool3;