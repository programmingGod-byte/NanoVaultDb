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

class alignas(CACHELINE) KeltnerChannels {

private:
  int64_t prev_ema = -1;
  int64_t prev_atr = -1;
  int64_t prev_close = -1;
  
  int64_t sum_ema = 0;
  int64_t sum_tr = 0;

  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t window = 20;
  int64_t atr_window = 14;
  double multiplier = 2.0;
  
  int64_t filled_ema = 0;
  int64_t filled_atr = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = Upper, values[1] = Middle, values[2] = Lower
  std::vector<double> values{-1.0, -1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  KeltnerChannels(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (usedColumns.size() >= 3 && usedColumns[0] > 0) {
      high_col = usedColumns[0];
      low_col = usedColumns[1];
      close_col = usedColumns[2];
    } else {
      high_col = 3;
      low_col = 4;
      close_col = 2;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), window);
    }
    if (params.size() > 1) {
      multiplier = std::stod(params[1]);
    }
    if (params.size() > 2) {
      auto [ptr, ec] = std::from_chars(params[2].data(), params[2].data() + params[2].size(), atr_window);
    }
    
    this->prev_ema = -1;
    this->prev_atr = -1;
    this->prev_close = -1;
    this->sum_ema = 0;
    this->sum_tr = 0;
    this->filled_ema = 0;
    this->filled_atr = 0;
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[high_col].head;
    
    int64_t current_high = tableColumn->history[high_col].get(head);
    int64_t current_low = tableColumn->history[low_col].get(head);
    int64_t current_close = tableColumn->history[close_col].get(head);

    // Update EMA
    if (filled_ema < window) {
      sum_ema += current_close;
      filled_ema++;
      if (filled_ema == window) prev_ema = sum_ema / window;
    } else {
      prev_ema = (current_close * 2 + prev_ema * (window - 1)) / (window + 1);
    }

    // Update ATR
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

    if (filled_atr < atr_window) {
      sum_tr += tr;
      filled_atr++;
      if (filled_atr == atr_window) prev_atr = sum_tr / atr_window;
    } else {
      prev_atr = (prev_atr * (atr_window - 1) + tr) / atr_window;
    }

    if (filled_ema >= window && filled_atr >= atr_window && precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[close_col];
      
      double ema_double = MyUtility::from_fixed(prev_ema, current_precision);
      double atr_double = MyUtility::from_fixed(prev_atr, current_precision);

      values[0] = ema_double + (multiplier * atr_double); // Upper Band
      values[1] = ema_double;                             // Middle Band
      values[2] = ema_double - (multiplier * atr_double); // Lower Band
    }

    DEBUG_LOG("[DEBUG_VALUES] KeltnerChannels calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<KeltnerChannels *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<KeltnerChannels *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<KeltnerChannels *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &KeltnerChannels::run;
    e.result_fn = &KeltnerChannels::get_result;
    e.vector_fn = &KeltnerChannels::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
