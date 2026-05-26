#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>

class alignas(CACHELINE) DonchianChannels {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t window = 20;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  std::vector<int> usedColumns;

  // values[0] = Upper Band, values[1] = Middle Band, values[2] = Lower Band
  std::vector<double> values{-1.0, -1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  DonchianChannels(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN) return;

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

    if (precision && high_col >= 0 && high_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[high_col];
      
      double upper = MyUtility::from_fixed(highest_high, current_precision);
      double lower = MyUtility::from_fixed(lowest_low, current_precision);
      double middle = (upper + lower) / 2.0;

      values[0] = upper;
      values[1] = middle;
      values[2] = lower;
    }

    DEBUG_LOG("[DEBUG_VALUES] DonchianChannels calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<DonchianChannels *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<DonchianChannels *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<DonchianChannels *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &DonchianChannels::run;
    e.result_fn = &DonchianChannels::get_result;
    e.vector_fn = &DonchianChannels::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
