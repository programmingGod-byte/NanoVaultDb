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

class alignas(CACHELINE) ADX {

private:
  int64_t prev_high = -1;
  int64_t prev_low = -1;
  int64_t prev_close = -1;

  int64_t sum_tr = 0;
  int64_t sum_pos_dm = 0;
  int64_t sum_neg_dm = 0;
  
  int64_t smooth_tr = 0;
  int64_t smooth_pos_dm = 0;
  int64_t smooth_neg_dm = 0;
  int64_t smooth_adx = 0;
  int64_t sum_dx = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14;
  int64_t filled = 0;
  int64_t adx_filled = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = ADX, values[1] = +DI, values[2] = -DI
  std::vector<double> values{-1.0, -1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  ADX(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    this->prev_high = -1;
    this->prev_low = -1;
    this->prev_close = -1;
    this->sum_tr = 0;
    this->sum_pos_dm = 0;
    this->sum_neg_dm = 0;
    this->smooth_tr = 0;
    this->smooth_pos_dm = 0;
    this->smooth_neg_dm = 0;
    this->smooth_adx = 0;
    this->sum_dx = 0;
    this->filled = 0;
    this->adx_filled = 0;
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

    if (UNLIKELY(prev_close == -1)) {
      prev_high = current_high;
      prev_low = current_low;
      prev_close = current_close;
      return;
    }

    int64_t tr1 = current_high - current_low;
    int64_t tr2 = std::abs(current_high - prev_close);
    int64_t tr3 = std::abs(current_low - prev_close);
    int64_t tr = std::max({tr1, tr2, tr3});

    int64_t up_move = current_high - prev_high;
    int64_t down_move = prev_low - current_low;

    int64_t pos_dm = 0;
    int64_t neg_dm = 0;

    if (up_move > down_move && up_move > 0) {
      pos_dm = up_move;
    }
    if (down_move > up_move && down_move > 0) {
      neg_dm = down_move;
    }

    prev_high = current_high;
    prev_low = current_low;
    prev_close = current_close;

    if (filled < window) {
      sum_tr += tr;
      sum_pos_dm += pos_dm;
      sum_neg_dm += neg_dm;
      filled++;
      if (filled == window) {
        smooth_tr = sum_tr;
        smooth_pos_dm = sum_pos_dm;
        smooth_neg_dm = sum_neg_dm;
      }
      return;
    } else {
      // Wilder's Smoothing: smooth = smooth - (smooth / window) + current
      smooth_tr = smooth_tr - (smooth_tr / window) + tr;
      smooth_pos_dm = smooth_pos_dm - (smooth_pos_dm / window) + pos_dm;
      smooth_neg_dm = smooth_neg_dm - (smooth_neg_dm / window) + neg_dm;
    }

    if (smooth_tr > 0) {
      double pos_di = 100.0 * static_cast<double>(smooth_pos_dm) / static_cast<double>(smooth_tr);
      double neg_di = 100.0 * static_cast<double>(smooth_neg_dm) / static_cast<double>(smooth_tr);
      
      double dx = 0;
      if (pos_di + neg_di > 0) {
        dx = 100.0 * std::abs(pos_di - neg_di) / (pos_di + neg_di);
      }
      
      int64_t dx_scaled = static_cast<int64_t>(dx * 10000); // Scale up for integer math

      if (adx_filled < window) {
        sum_dx += dx_scaled;
        adx_filled++;
        if (adx_filled == window) {
          smooth_adx = sum_dx / window;
        }
      } else {
        smooth_adx = (smooth_adx * (window - 1) + dx_scaled) / window;
      }

      values[0] = static_cast<double>(smooth_adx) / 10000.0;
      values[1] = pos_di;
      values[2] = neg_di;
    }

    DEBUG_LOG("[DEBUG_VALUES] ADX calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<ADX *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<ADX *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<ADX *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &ADX::run;
    e.result_fn = &ADX::get_result;
    e.vector_fn = &ADX::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
