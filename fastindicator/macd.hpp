#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <cmath>


class alignas(CACHELINE) MACD {

private:
  int64_t prev_fast_ema = -1;
  int64_t prev_slow_ema = -1;
  int64_t prev_signal_ema = -1;

  int64_t sum_fast = 0;
  int64_t sum_slow = 0;
  int64_t sum_signal = 0;

  int64_t tick = 0;
  int64_t count = 0;

  int64_t fast_window = 12;
  int64_t slow_window = 26;
  int64_t signal_window = 9;

  int64_t filled_fast = 0;
  int64_t filled_slow = 0;
  int64_t filled_signal = 0;
  
  int64_t column_to_use = -1;
  std::vector<int> usedColumns;

  // values[0] = MACD Line, values[1] = Signal Line, values[2] = Histogram
  std::vector<double> values{-1.0, -1.0, -1.0};

  HFT::ColumnRing *columnRing = nullptr;
  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  MACD(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;

    if (LIKELY(column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN))
      this->column_to_use = column_to_use;
    else if (usedColumns.size() > 0 && usedColumns[0] > 0)
      this->column_to_use = usedColumns[0];
    else
      this->column_to_use = 2; // Default to close price column

    this->tableColumn = &tableColumn;

    if (LIKELY(this->column_to_use >= 0 && this->column_to_use < HFT::MAXCOLUMN))
      this->columnRing = &tableColumn.history[this->column_to_use];
    else
      this->columnRing = nullptr;

    this->precision = tableColumn.precisions;
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), fast_window);
    }
    if (params.size() > 1) {
      auto [ptr, ec] = std::from_chars(params[1].data(), params[1].data() + params[1].size(), slow_window);
    }
    if (params.size() > 2) {
      auto [ptr, ec] = std::from_chars(params[2].data(), params[2].data() + params[2].size(), signal_window);
    }

    this->prev_fast_ema = -1;
    this->prev_slow_ema = -1;
    this->prev_signal_ema = -1;
    this->sum_fast = 0;
    this->sum_slow = 0;
    this->sum_signal = 0;
    this->filled_fast = 0;
    this->filled_slow = 0;
    this->filled_signal = 0;

    DEBUG_LOG("[MACD] Windows set to: Fast=" << fast_window << " Slow=" << slow_window << " Signal=" << signal_window);
  }

  inline int64_t result() const {
    if (filled_slow == slow_window) return prev_fast_ema - prev_slow_ema;
    return 0;
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    if (!columnRing) return;
    
    count++;
    if (count != tick) return;
    count = 0;

    const int64_t latest = *columnRing->latest_ptr();

    // Fast EMA
    if (filled_fast < fast_window) {
      sum_fast += latest;
      filled_fast++;
      if (filled_fast == fast_window) prev_fast_ema = sum_fast / fast_window;
    } else {
      prev_fast_ema = (latest * 2 + prev_fast_ema * (fast_window - 1)) / (fast_window + 1);
    }

    // Slow EMA
    if (filled_slow < slow_window) {
      sum_slow += latest;
      filled_slow++;
      if (filled_slow == slow_window) prev_slow_ema = sum_slow / slow_window;
    } else {
      prev_slow_ema = (latest * 2 + prev_slow_ema * (slow_window - 1)) / (slow_window + 1);
    }

    if (filled_slow == slow_window) {
      int64_t macd_line = prev_fast_ema - prev_slow_ema;

      // Signal EMA
      if (filled_signal < signal_window) {
        sum_signal += macd_line;
        filled_signal++;
        if (filled_signal == signal_window) prev_signal_ema = sum_signal / signal_window;
      } else {
        prev_signal_ema = (macd_line * 2 + prev_signal_ema * (signal_window - 1)) / (signal_window + 1);
      }

      if (precision && column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN) {
        int64_t current_precision = precision[column_to_use];
        double p10 = MyUtility::POW10[current_precision];
        values[0] = static_cast<double>(macd_line) / p10;
        
        if (filled_signal == signal_window) {
          values[1] = static_cast<double>(prev_signal_ema) / p10;
          values[2] = values[0] - values[1]; // Histogram
        }
      }
    }

    DEBUG_LOG("[DEBUG_VALUES] MACD calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<MACD *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<MACD *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<MACD *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &MACD::run;
    e.result_fn = &MACD::get_result;
    e.vector_fn = &MACD::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
