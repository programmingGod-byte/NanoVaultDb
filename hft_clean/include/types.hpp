#pragma once
/*
 * types.hpp  –  Core primitive types for the HFT order book.
 *
 * Design goals:
 *   - Zero heap allocation in the hot path
 *   - Fixed-point arithmetic (no FP on prices/quantities in matching loop)
 *   - Cache-line aligned hot structs (Order = 128 B, PriceLevel = 64 B)
 *   - Compiler hints: LIKELY/UNLIKELY, FORCE_INLINE, PREFETCH
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>

// ---------------------------------------------------------------------------
// Compiler hints
// ---------------------------------------------------------------------------
#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#define FORCE_INLINE __attribute__((always_inline)) inline
#define CACHE_LINE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE)))
#define PREFETCH_R(p) __builtin_prefetch((p), 0, 3)
#define PREFETCH_W(p) __builtin_prefetch((p), 1, 3)

namespace Book {

// ---------------------------------------------------------------------------
// Scalar aliases – keep types explicit so we never mix price with quantity
// ---------------------------------------------------------------------------
using Price = int64_t;    // fixed-point, scale = 1e8
using Quantity = int64_t; // fixed-point, scale = 1e8
using OrderId = uint64_t;
using TradeId = uint64_t;
using Nanos = uint64_t; // nanoseconds since epoch

// Fixed-point scale: 1 unit = 1e-8  (supports satoshi & paise precision)
static constexpr int64_t PRICE_SCALE = 100'000'000LL;

// ---------------------------------------------------------------------------
// Conversion helpers  (only call from outside the hot-path)
// ---------------------------------------------------------------------------
FORCE_INLINE Price to_price(double d) noexcept {
  return static_cast<Price>(d * PRICE_SCALE + 0.5);
}
FORCE_INLINE double from_price(Price p) noexcept {
  return static_cast<double>(p) / PRICE_SCALE;
}
FORCE_INLINE Quantity to_qty(double d) noexcept {
  return static_cast<Quantity>(d * PRICE_SCALE + 0.5);
}
FORCE_INLINE double from_qty(Quantity q) noexcept {
  return static_cast<double>(q) / PRICE_SCALE;
}

FORCE_INLINE Nanos now_ns() noexcept {
  using namespace std::chrono;
  return static_cast<Nanos>(
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
          .count());
}

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------
enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t {
  LIMIT = 0,
  MARKET = 1,
  IOC = 2,
  FOK = 3,
  POST_ONLY = 4
};
enum class OrderStatus : uint8_t {
  NEW = 0,
  PARTIAL = 1,
  FILLED = 2,
  CANCELLED = 3,
  REJECTED = 4
};
enum class Exchange : uint8_t {
  GENERIC = 0,
  BINANCE = 1,
  ZERODHA = 2,
  COINBASE = 3,
  BYBIT = 4
};

FORCE_INLINE constexpr Side opposite(Side s) noexcept {
  return s == Side::BUY ? Side::SELL : Side::BUY;
}

// ---------------------------------------------------------------------------
// Symbol  – fixed 24-byte stack string, no heap allocation
// ---------------------------------------------------------------------------
struct Symbol {
  static constexpr size_t MAX_LEN = 24;
  char data[MAX_LEN]{};

  Symbol() = default;
  explicit Symbol(std::string_view sv) noexcept {
    size_t n = (sv.size() < MAX_LEN) ? sv.size() : MAX_LEN - 1;
    std::memcpy(data, sv.data(), n);
    data[n] = '\0';
  }
  FORCE_INLINE std::string_view view() const noexcept {
    return std::string_view{data};
  }
  FORCE_INLINE bool operator==(const Symbol &o) const noexcept {
    return std::memcmp(data, o.data, MAX_LEN) == 0;
  }
};

// ---------------------------------------------------------------------------
// Order  – 128 bytes = 2 cache lines.
//   First 64 B holds all hot fields (id, price, qty, remaining, ts, links).
//   Second 64 B holds cold fields (side, type, status, exchange, symbol).
// ---------------------------------------------------------------------------
struct alignas(64) Order {
  // --- Cache line 0 (hot) ---
  OrderId id{0};
  Price price{0};
  Quantity qty{0};
  Quantity remaining{0};
  Nanos timestamp{0};
  OrderId prev{0};
  OrderId next{0};
  // 8 bytes padding to fill the line
  uint64_t _hot_pad{0};

  // --- Cache line 1 (cold) ---
  Side side{Side::BUY};
  OrderType type{OrderType::LIMIT};
  OrderStatus status{OrderStatus::NEW};
  Exchange exchange{Exchange::GENERIC};
  uint8_t _cold_pad0[4]{};
  Symbol symbol{};
  uint8_t _cold_pad1[4]{};

  FORCE_INLINE bool is_active() const noexcept {
    return status == OrderStatus::NEW || status == OrderStatus::PARTIAL;
  }
};
static_assert(sizeof(Order) == 128,
              "Order must be exactly 128 bytes (2 cache lines)");

// ---------------------------------------------------------------------------
// Trade  – fill record emitted by the matching engine
// ---------------------------------------------------------------------------
struct Trade {
  /*
      maker_id → the order already sitting in the book (provides liquidity)
      taker_id → the incoming order that matched (removes liquidity)
  */
  TradeId id{0};
  OrderId maker_id{0};
  OrderId taker_id{0};
  Price price{0};
  Quantity qty{0};
  Nanos timestamp{0};
  /*
    Was the incoming (taker) order a BUY or a SELL?
    */
  Side aggressor{Side::BUY};
  uint8_t _pad[7]{};
};

// ---------------------------------------------------------------------------
// BBO  – best bid/offer snapshot
// ---------------------------------------------------------------------------
struct BBO {
  Price bid_price{0};
  Price ask_price{0};
  Quantity bid_qty{0};
  Quantity ask_qty{0};
  Nanos timestamp{0};

  FORCE_INLINE bool valid() const noexcept {
    return bid_price > 0 && ask_price > 0;
  }
  FORCE_INLINE Price spread() const noexcept { return ask_price - bid_price; }
};

} // namespace Book
