#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

class alignas(CACHELINE) ATR {

private:
  int64_t prev_atr = -1;
  int64_t prev_close = -1;
  int64_t sum_tr = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14; // Default ATR window is 14
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = ATR value
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  ATR(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    // Use passed usedColumn vector for multi-column mapping
    if (usedColumns.size() >= 3 && usedColumns[0] > 0) {
      high_col = usedColumns[0];
      low_col = usedColumns[1];
      close_col = usedColumns[2];
    } else {
      // Default fallback for btc_ohlc format: High=2, Low=3, Close=1
      high_col = 3;
      low_col = 4;
      close_col = 2;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), window);
      if (ec != std::errc() || window <= 0) throw std::runtime_error("Invalid ATR window parameter");
    }

    this->prev_atr = -1;
    this->prev_close = -1;
    this->sum_tr = 0;
    this->filled = 0;

    DEBUG_LOG("[ATR] Window set to: " << this->window << " Cols: H=" << high_col << " L=" << low_col << " C=" << close_col);
  }

  inline int64_t result() const {
    if (filled < window && filled > 0) return sum_tr / filled;
    return prev_atr == -1 ? 0 : prev_atr;
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN) return;

    int64_t current_high = *tableColumn->history[high_col].latest_ptr();
    int64_t current_low = *tableColumn->history[low_col].latest_ptr();
    int64_t current_close = *tableColumn->history[close_col].latest_ptr();

    int64_t tr = 0;
    if (prev_close == -1) {
      tr = current_high - current_low;
    } else {
      int64_t tr1 = current_high - current_low;
      int64_t tr2 = std::abs(current_high - prev_close);
      int64_t tr3 = std::abs(current_low - prev_close);
      tr = std::max({tr1, tr2, tr3});
    }
    prev_close = current_close;

    // RMA Smoothing (Wilder's)
    if (filled < window) {
      sum_tr += tr;
      filled++;
      if (filled == window) {
        prev_atr = sum_tr / window;
      }
    } else {
      prev_atr = (prev_atr * (window - 1) + tr) / window;
    }

    if (precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[close_col];
      values[0] = MyUtility::from_fixed(result(), current_precision);
    }

    DEBUG_LOG("[DEBUG_VALUES] ATR calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<ATR *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<ATR *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<ATR *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &ATR::run;
    e.result_fn = &ATR::get_result;
    e.vector_fn = &ATR::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
