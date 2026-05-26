#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>

class alignas(CACHELINE) Stochastic {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t k_window = 14;
  int64_t d_window = 3;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // Buffer to compute SMA for %D
  std::vector<double> k_history;
  int64_t k_history_head = 0;
  int64_t k_filled = 0;

  // values[0] = %K, values[1] = %D
  std::vector<double> values{-1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  Stochastic(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    
    k_history.resize(d_window, 0.0);
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), k_window);
    }
    if (params.size() > 1) {
      auto [ptr, ec] = std::from_chars(params[1].data(), params[1].data() + params[1].size(), d_window);
    }

    this->filled = 0;
    this->k_history_head = 0;
    this->k_filled = 0;
    this->k_history.resize(d_window, 0.0);

    DEBUG_LOG("[Stochastic] Windows set to: K=" << k_window << " D=" << d_window << " Cols: H=" << high_col << " L=" << low_col << " C=" << close_col);
  }

  inline int64_t result() const {
    // Return %K scaled by 1e8 for backwards compatibility if needed
    return static_cast<int64_t>(values[0] * 100000000.0);
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN) return;

    filled++;
    
    int64_t highest_high = 0;
    int64_t lowest_low = INT64_MAX;

    int32_t head = tableColumn->history[high_col].head;
    int64_t items_to_check = std::min(filled, k_window);

    // O(N) scan for min/max over small window
    for (int i = 0; i < items_to_check; i++) {
      int32_t idx = (head - i) & HFT::MAXRINGMASK;
      int64_t h = tableColumn->history[high_col].get(idx);
      int64_t l = tableColumn->history[low_col].get(idx);
      
      if (h > highest_high) highest_high = h;
      if (l < lowest_low) lowest_low = l;
    }

    int64_t diff = highest_high - lowest_low;
    double k_val = 50.0; // Default if no movement

    if (diff > 0) {
      int64_t current_close = *tableColumn->history[close_col].latest_ptr();
      k_val = static_cast<double>(current_close - lowest_low) / static_cast<double>(diff) * 100.0;
    }

    values[0] = k_val;

    // Update %D SMA
    k_history[k_history_head] = k_val;
    k_history_head = (k_history_head + 1) % d_window;
    
    if (k_filled < d_window) k_filled++;
    
    if (k_filled == d_window) {
      double sum_k = 0;
      for (double val : k_history) sum_k += val;
      values[1] = sum_k / d_window;
    }

    DEBUG_LOG("[DEBUG_VALUES] Stochastic calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<Stochastic *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<Stochastic *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<Stochastic *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &Stochastic::run;
    e.result_fn = &Stochastic::get_result;
    e.vector_fn = &Stochastic::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
