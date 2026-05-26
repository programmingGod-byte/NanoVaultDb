#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) VWAP {

private:
  __int128_t sum_pv = 0;
  __int128_t sum_v = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14; // Rolling VWAP window
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  int64_t vol_col = 4;
  std::vector<int> usedColumns;

  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  VWAP(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (usedColumns.size() >= 4 && usedColumns[0] > 0) {
      high_col = usedColumns[0];
      low_col = usedColumns[1];
      close_col = usedColumns[2];
      vol_col = usedColumns[3];
    } else {
      high_col = 3;
      low_col = 4;
      close_col = 2;
      vol_col = 5;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), window);
      if (ec != std::errc() || window <= 0) throw std::runtime_error("Invalid VWAP window parameter");
    }

    this->sum_pv = 0;
    this->sum_v = 0;
    this->filled = 0;

    DEBUG_LOG("[VWAP] Window set to: " << this->window << " Cols: H=" << high_col << " L=" << low_col << " C=" << close_col << " V=" << vol_col);
  }

  inline int64_t result() const {
    if (sum_v == 0) return 0;
    return static_cast<int64_t>(sum_pv / sum_v);
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN || vol_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[high_col].head;
    
    int64_t current_high = tableColumn->history[high_col].get(head);
    int64_t current_low = tableColumn->history[low_col].get(head);
    int64_t current_close = tableColumn->history[close_col].get(head);
    int64_t current_vol = tableColumn->history[vol_col].get(head);

    int64_t typical_price = (current_high + current_low + current_close) / 3;

    sum_pv += static_cast<__int128_t>(typical_price) * current_vol;
    sum_v += current_vol;

    if (filled < window) {
      filled++;
    } else {
      int32_t old_idx = (head - window - 1) & HFT::MAXRINGMASK;
      int64_t old_high = tableColumn->history[high_col].get(old_idx);
      int64_t old_low = tableColumn->history[low_col].get(old_idx);
      int64_t old_close = tableColumn->history[close_col].get(old_idx);
      int64_t old_vol = tableColumn->history[vol_col].get(old_idx);
      
      int64_t old_typical = (old_high + old_low + old_close) / 3;
      
      sum_pv -= static_cast<__int128_t>(old_typical) * old_vol;
      sum_v -= old_vol;
    }

    if (precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[close_col];
      values[0] = MyUtility::from_fixed(result(), current_precision);
    }

    DEBUG_LOG("[DEBUG_VALUES] VWAP calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<VWAP *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<VWAP *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<VWAP *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &VWAP::run;
    e.result_fn = &VWAP::get_result;
    e.vector_fn = &VWAP::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
