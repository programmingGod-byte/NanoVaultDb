#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) AwesomeOscillator {

private:
  int64_t sum_fast = 0;
  int64_t sum_slow = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t fast_window = 5;
  int64_t slow_window = 34;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  std::vector<int> usedColumns;

  // values[0] = Awesome Oscillator
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  AwesomeOscillator(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (usedColumns.size() >= 2 && usedColumns[0] > 0) {
      high_col = usedColumns[0];
      low_col = usedColumns[1];
    } else {
      high_col = 3;
      low_col = 4;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), fast_window);
    }
    if (params.size() > 1) {
      auto [ptr, ec] = std::from_chars(params[1].data(), params[1].data() + params[1].size(), slow_window);
    }
    this->sum_fast = 0;
    this->sum_slow = 0;
    this->filled = 0;
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[high_col].head;
    
    int64_t current_high = tableColumn->history[high_col].get(head);
    int64_t current_low = tableColumn->history[low_col].get(head);
    int64_t median_price = (current_high + current_low) / 2;

    sum_fast += median_price;
    sum_slow += median_price;

    if (filled < slow_window) {
      filled++;
      
      // Keep fast sum bounded to fast_window
      if (filled > fast_window) {
        int32_t old_idx_fast = (head - fast_window - 1) & HFT::MAXRINGMASK;
        int64_t old_high = tableColumn->history[high_col].get(old_idx_fast);
        int64_t old_low = tableColumn->history[low_col].get(old_idx_fast);
        sum_fast -= (old_high + old_low) / 2;
      }
      
    } else {
      int32_t old_idx_fast = (head - fast_window - 1) & HFT::MAXRINGMASK;
      int64_t old_high_fast = tableColumn->history[high_col].get(old_idx_fast);
      int64_t old_low_fast = tableColumn->history[low_col].get(old_idx_fast);
      sum_fast -= (old_high_fast + old_low_fast) / 2;
      
      int32_t old_idx_slow = (head - slow_window - 1) & HFT::MAXRINGMASK;
      int64_t old_high_slow = tableColumn->history[high_col].get(old_idx_slow);
      int64_t old_low_slow = tableColumn->history[low_col].get(old_idx_slow);
      sum_slow -= (old_high_slow + old_low_slow) / 2;
    }

    if (filled >= fast_window && precision && high_col >= 0 && high_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[high_col];
      
      int64_t sma_fast = sum_fast / fast_window;
      int64_t sma_slow = sum_slow / (filled < slow_window ? filled : slow_window);
      
      double sma_fast_double = MyUtility::from_fixed(sma_fast, current_precision);
      double sma_slow_double = MyUtility::from_fixed(sma_slow, current_precision);

      values[0] = sma_fast_double - sma_slow_double;
    }

    DEBUG_LOG("[DEBUG_VALUES] AwesomeOscillator calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<AwesomeOscillator *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<AwesomeOscillator *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<AwesomeOscillator *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &AwesomeOscillator::run;
    e.result_fn = &AwesomeOscillator::get_result;
    e.vector_fn = &AwesomeOscillator::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
