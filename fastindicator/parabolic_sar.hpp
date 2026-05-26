#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>
#include <algorithm>

class alignas(CACHELINE) ParabolicSAR {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  bool is_long = true;
  double af = 0.02;
  double af_step = 0.02;
  double af_max = 0.20;
  
  double ep = -1.0;
  double sar = -1.0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  std::vector<int> usedColumns;

  // values[0] = SAR
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  ParabolicSAR(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
      af_step = std::stod(params[0]);
    }
    if (params.size() > 1) {
      af_max = std::stod(params[1]);
    }
    this->af = af_step;
    this->ep = -1.0;
    this->sar = -1.0;
    this->is_long = true;
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[high_col].head;
    
    int64_t current_high_fixed = tableColumn->history[high_col].get(head);
    int64_t current_low_fixed = tableColumn->history[low_col].get(head);

    if (precision && high_col >= 0 && high_col < HFT::MAXCOLUMN) {
      int64_t p = precision[high_col];
      double current_high = MyUtility::from_fixed(current_high_fixed, p);
      double current_low = MyUtility::from_fixed(current_low_fixed, p);

      if (UNLIKELY(sar == -1.0)) {
        is_long = true;
        sar = current_low;
        ep = current_high;
        af = af_step;
        values[0] = sar;
        return;
      }

      // Calculate next SAR
      double next_sar = sar + af * (ep - sar);

      if (is_long) {
        if (current_low < next_sar) {
          // Flip to short
          is_long = false;
          next_sar = ep;
          ep = current_low;
          af = af_step;
        } else {
          if (current_high > ep) {
            ep = current_high;
            af = std::min(af + af_step, af_max);
          }
          // Cap SAR at previous 2 lows
          int32_t prev1 = (head - 1) & HFT::MAXRINGMASK;
          int32_t prev2 = (head - 2) & HFT::MAXRINGMASK;
          double l1 = MyUtility::from_fixed(tableColumn->history[low_col].get(prev1), p);
          double l2 = MyUtility::from_fixed(tableColumn->history[low_col].get(prev2), p);
          next_sar = std::min({next_sar, l1, l2});
        }
      } else {
        if (current_high > next_sar) {
          // Flip to long
          is_long = true;
          next_sar = ep;
          ep = current_high;
          af = af_step;
        } else {
          if (current_low < ep) {
            ep = current_low;
            af = std::min(af + af_step, af_max);
          }
          // Cap SAR at previous 2 highs
          int32_t prev1 = (head - 1) & HFT::MAXRINGMASK;
          int32_t prev2 = (head - 2) & HFT::MAXRINGMASK;
          double h1 = MyUtility::from_fixed(tableColumn->history[high_col].get(prev1), p);
          double h2 = MyUtility::from_fixed(tableColumn->history[high_col].get(prev2), p);
          next_sar = std::max({next_sar, h1, h2});
        }
      }

      sar = next_sar;
      values[0] = sar;
    }

    DEBUG_LOG("[DEBUG_VALUES] ParabolicSAR calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<ParabolicSAR *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<ParabolicSAR *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<ParabolicSAR *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &ParabolicSAR::run;
    e.result_fn = &ParabolicSAR::get_result;
    e.vector_fn = &ParabolicSAR::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
