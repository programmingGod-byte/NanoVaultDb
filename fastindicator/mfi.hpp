#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) MFI {

private:
  int64_t prev_tp = -1;
  
  __int128_t running_pos_flow = 0;
  __int128_t running_neg_flow = 0;
  __int128_t avg_pos_flow = 0;
  __int128_t avg_neg_flow = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14;
  int64_t filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  int64_t vol_col = 4;
  std::vector<int> usedColumns;

  // values[0] = MFI
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  MFI(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    }
    this->prev_tp = -1;
    this->running_pos_flow = 0;
    this->running_neg_flow = 0;
    this->avg_pos_flow = 0;
    this->avg_neg_flow = 0;
    this->filled = 0;
  }

  inline int64_t result() const { return 0; }
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

    int64_t tp = (current_high + current_low + current_close) / 3;

    if (UNLIKELY(prev_tp == -1)) {
      prev_tp = tp;
      values[0] = 50.0;
      return;
    }

    __int128_t money_flow = static_cast<__int128_t>(tp) * current_vol;
    __int128_t pos_flow = (tp > prev_tp) ? money_flow : 0;
    __int128_t neg_flow = (tp < prev_tp) ? money_flow : 0;
    
    prev_tp = tp;

    if (filled < window) {
      running_pos_flow += pos_flow;
      running_neg_flow += neg_flow;
      filled++;
      
      if (filled == window) {
        avg_pos_flow = running_pos_flow / window;
        avg_neg_flow = running_neg_flow / window;
      }
    } else {
      avg_pos_flow = (avg_pos_flow * (window - 1) + pos_flow) / window;
      avg_neg_flow = (avg_neg_flow * (window - 1) + neg_flow) / window;
    }

    if (filled == window) {
      if (avg_neg_flow == 0) {
        values[0] = 100.0;
      } else {
        double pos_double = static_cast<double>(avg_pos_flow);
        double neg_double = static_cast<double>(avg_neg_flow);
        values[0] = 100.0 - (100.0 / (1.0 + (pos_double / neg_double)));
      }
    } else {
      values[0] = 50.0;
    }

    DEBUG_LOG("[DEBUG_VALUES] MFI calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<MFI *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<MFI *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<MFI *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &MFI::run;
    e.result_fn = &MFI::get_result;
    e.vector_fn = &MFI::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
