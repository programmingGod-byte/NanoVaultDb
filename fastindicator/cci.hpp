#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <cmath>

class alignas(CACHELINE) CCI {

private:
  __int128_t sum_tp = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 20;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = CCI
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  CCI(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    this->sum_tp = 0;
    this->filled = 0;
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

    int64_t typical_price = (current_high + current_low + current_close) / 3;

    sum_tp += typical_price;

    if (filled < window) {
      filled++;
    } else {
      int32_t old_idx = (head - window - 1) & HFT::MAXRINGMASK;
      int64_t old_high = tableColumn->history[high_col].get(old_idx);
      int64_t old_low = tableColumn->history[low_col].get(old_idx);
      int64_t old_close = tableColumn->history[close_col].get(old_idx);
      int64_t old_tp = (old_high + old_low + old_close) / 3;
      sum_tp -= old_tp;
    }

    if (filled > 0 && precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t current_window = filled < window ? filled : window;
      int64_t sma_tp = static_cast<int64_t>(sum_tp / current_window);

      __int128_t mean_deviation_sum = 0;
      int64_t items_to_check = std::min(filled, window);
      
      // Compute Mean Deviation via O(N) loop
      for (int i = 0; i < items_to_check; i++) {
        int32_t idx = (head - i) & HFT::MAXRINGMASK;
        int64_t h = tableColumn->history[high_col].get(idx);
        int64_t l = tableColumn->history[low_col].get(idx);
        int64_t c = tableColumn->history[close_col].get(idx);
        int64_t tp_i = (h + l + c) / 3;
        mean_deviation_sum += std::abs(tp_i - sma_tp);
      }

      double mean_dev_double = static_cast<double>(mean_deviation_sum) / current_window;
      double tp_double = static_cast<double>(typical_price);
      double sma_tp_double = static_cast<double>(sma_tp);
      
      // If mean deviation is 0, avoid division by zero
      if (mean_dev_double == 0) {
        values[0] = 0.0;
      } else {
        // CCI = (TP - SMA(TP)) / (0.015 * Mean Deviation)
        values[0] = (tp_double - sma_tp_double) / (0.015 * mean_dev_double);
      }
    }

    DEBUG_LOG("[DEBUG_VALUES] CCI calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<CCI *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<CCI *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<CCI *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &CCI::run;
    e.result_fn = &CCI::get_result;
    e.vector_fn = &CCI::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
