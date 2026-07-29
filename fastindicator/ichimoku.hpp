#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>


class alignas(CACHELINE) Ichimoku {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t tenkan_window = 9;
  int64_t kijun_window = 26;
  int64_t span_b_window = 52;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values: Tenkan, Kijun, Span A, Span B, Chikou
  std::vector<double> values{-1.0, -1.0, -1.0, -1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  Ichimoku(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), tenkan_window);
    }
    if (params.size() > 1) {
      auto [ptr, ec] = std::from_chars(params[1].data(), params[1].data() + params[1].size(), kijun_window);
    }
    if (params.size() > 2) {
      auto [ptr, ec] = std::from_chars(params[2].data(), params[2].data() + params[2].size(), span_b_window);
    }
    this->filled = 0;
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN) return;

    filled++;
    
    int32_t head = tableColumn->history[high_col].head;
    
    auto get_donchian_mid = [&](int64_t window) {
      int64_t items_to_check = std::min(filled, window);
      int64_t highest_high = 0;
      int64_t lowest_low = INT64_MAX;
      
      for (int i = 0; i < items_to_check; i++) {
        int32_t idx = (head - i) & HFT::MAXRINGMASK;
        int64_t h = tableColumn->history[high_col].get(idx);
        int64_t l = tableColumn->history[low_col].get(idx);
        
        if (h > highest_high) highest_high = h;
        if (l < lowest_low) lowest_low = l;
      }
      return (highest_high + lowest_low) / 2;
    };

    if (precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t p = precision[close_col];
      
      int64_t tenkan_fixed = get_donchian_mid(tenkan_window);
      int64_t kijun_fixed = get_donchian_mid(kijun_window);
      int64_t span_b_fixed = get_donchian_mid(span_b_window);
      
      double tenkan = MyUtility::from_fixed(tenkan_fixed, p);
      double kijun = MyUtility::from_fixed(kijun_fixed, p);
      double span_a = (tenkan + kijun) / 2.0;
      double span_b = MyUtility::from_fixed(span_b_fixed, p);
      
      int64_t current_close = tableColumn->history[close_col].get(head);
      double chikou = MyUtility::from_fixed(current_close, p);

      values[0] = tenkan;
      values[1] = kijun;
      values[2] = span_a;
      values[3] = span_b;
      values[4] = chikou; // The frontend will shift this backwards
    }

    DEBUG_LOG("[DEBUG_VALUES] Ichimoku calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<Ichimoku *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<Ichimoku *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<Ichimoku *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &Ichimoku::run;
    e.result_fn = &Ichimoku::get_result;
    e.vector_fn = &Ichimoku::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
