#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>

class alignas(CACHELINE) WilliamsR {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t window = 14;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = %R
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  WilliamsR(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    
    int64_t highest_high = 0;
    int64_t lowest_low = INT64_MAX;

    int32_t head = tableColumn->history[high_col].head;
    int64_t items_to_check = std::min(filled, window);

    for (int i = 0; i < items_to_check; i++) {
      int32_t idx = (head - i) & HFT::MAXRINGMASK;
      int64_t h = tableColumn->history[high_col].get(idx);
      int64_t l = tableColumn->history[low_col].get(idx);
      
      if (h > highest_high) highest_high = h;
      if (l < lowest_low) lowest_low = l;
    }

    int64_t diff = highest_high - lowest_low;
    double r_val = -50.0;

    if (diff > 0) {
      int64_t current_close = *tableColumn->history[close_col].latest_ptr();
      r_val = static_cast<double>(highest_high - current_close) / static_cast<double>(diff) * -100.0;
    }

    values[0] = r_val;

    DEBUG_LOG("[DEBUG_VALUES] WilliamsR calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<WilliamsR *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<WilliamsR *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<WilliamsR *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &WilliamsR::run;
    e.result_fn = &WilliamsR::get_result;
    e.vector_fn = &WilliamsR::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
