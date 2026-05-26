#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) PivotPoints {

private:
  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t high_col = 2;
  int64_t low_col = 3;
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values: PP, R1, S1, R2, S2, R3, S3
  std::vector<double> values{-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  PivotPoints(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
    // Pivot points don't strictly require a window parameter for single-period calculation
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (high_col >= HFT::MAXCOLUMN || low_col >= HFT::MAXCOLUMN || close_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[high_col].head;
    int32_t prev_idx = (head - 1) & HFT::MAXRINGMASK;
    
    // We compute pivot points based on the previous period's OHLC
    int64_t prev_high = tableColumn->history[high_col].get(prev_idx);
    int64_t prev_low = tableColumn->history[low_col].get(prev_idx);
    int64_t prev_close = tableColumn->history[close_col].get(prev_idx);

    if (precision && close_col >= 0 && close_col < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[close_col];
      
      double ph = MyUtility::from_fixed(prev_high, current_precision);
      double pl = MyUtility::from_fixed(prev_low, current_precision);
      double pc = MyUtility::from_fixed(prev_close, current_precision);

      double pp = (ph + pl + pc) / 3.0;
      double r1 = (pp * 2.0) - pl;
      double s1 = (pp * 2.0) - ph;
      double r2 = pp + (ph - pl);
      double s2 = pp - (ph - pl);
      double r3 = ph + 2.0 * (pp - pl);
      double s3 = pl - 2.0 * (ph - pp);

      values[0] = pp;
      values[1] = r1;
      values[2] = s1;
      values[3] = r2;
      values[4] = s2;
      values[5] = r3;
      values[6] = s3;
    }

    DEBUG_LOG("[DEBUG_VALUES] PivotPoints calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<PivotPoints *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<PivotPoints *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<PivotPoints *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &PivotPoints::run;
    e.result_fn = &PivotPoints::get_result;
    e.vector_fn = &PivotPoints::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
