#pragma once

#include "hft.hpp"
#include "utils/types.hpp"
#include <cassert>
#include <cstddef>
#include <utility>
#include <new> 
#include "indicatorInclude.hpp"

template<typename T, std::size_t N>
struct alignas(CACHELINE) IndicatorPool {
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




static IndicatorPool<SMA, HFT::MAXHFTSYMBOL> smaPool;
static IndicatorPool<OBI, HFT::MAXHFTSYMBOL> obiPool;
static IndicatorPool<ADX, HFT::MAXHFTSYMBOL> adxPool;
static IndicatorPool<ATR, HFT::MAXHFTSYMBOL> atrPool;
static IndicatorPool<AwesomeOscillator, HFT::MAXHFTSYMBOL> awesomeOscillatorPool;
static IndicatorPool<BollingerBands, HFT::MAXHFTSYMBOL> bollingerBandsPool;
static IndicatorPool<CCI, HFT::MAXHFTSYMBOL> cciPool;
static IndicatorPool<DonchianChannels, HFT::MAXHFTSYMBOL> donchianChannelsPool;
static IndicatorPool<EMA, HFT::MAXHFTSYMBOL> emaPool;
static IndicatorPool<Ichimoku, HFT::MAXHFTSYMBOL> ichimokuPool;
static IndicatorPool<KeltnerChannels, HFT::MAXHFTSYMBOL> keltnerChannelsPool;
static IndicatorPool<MACD, HFT::MAXHFTSYMBOL> macdPool;
static IndicatorPool<MFI, HFT::MAXHFTSYMBOL> mfiPool;
static IndicatorPool<OBV, HFT::MAXHFTSYMBOL> obvPool;
static IndicatorPool<ParabolicSAR, HFT::MAXHFTSYMBOL> parabolicSARPool;
static IndicatorPool<PivotPoints, HFT::MAXHFTSYMBOL> pivotPointsPool;
static IndicatorPool<RSI, HFT::MAXHFTSYMBOL> rsiPool;
static IndicatorPool<Stochastic, HFT::MAXHFTSYMBOL> stochasticPool;
static IndicatorPool<TRIX, HFT::MAXHFTSYMBOL> trixPool;
static IndicatorPool<Volume, HFT::MAXHFTSYMBOL> volumePool;
static IndicatorPool<VWAP, HFT::MAXHFTSYMBOL> vwapPool;
static IndicatorPool<WilliamsR, HFT::MAXHFTSYMBOL> williamsRPool;