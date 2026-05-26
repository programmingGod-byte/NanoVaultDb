#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) OBV {

private:
  int64_t obv = 0;
  int64_t prev_close = -1;

  int64_t tick = 0;
  int64_t count = 0;
  
  int64_t close_col = 1;
  int64_t vol_col = 4;
  std::vector<int> usedColumns;

  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  OBV(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (usedColumns.size() >= 2 && usedColumns[0] > 0) {
      close_col = usedColumns[0];
      vol_col = usedColumns[1];
    } else {
      close_col = 2;
      vol_col = 5;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    this->obv = 0;
    this->prev_close = -1;
  }

  inline int64_t result() const {
    return obv;
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (close_col >= HFT::MAXCOLUMN || vol_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[close_col].head;
    
    int64_t current_close = tableColumn->history[close_col].get(head);
    int64_t current_vol = tableColumn->history[vol_col].get(head);

    if (prev_close != -1) {
      if (current_close > prev_close) {
        obv += current_vol;
      } else if (current_close < prev_close) {
        obv -= current_vol;
      }
    }
    
    prev_close = current_close;

    if (precision && vol_col >= 0 && vol_col < HFT::MAXCOLUMN) {
      int64_t vol_precision = precision[vol_col];
      values[0] = MyUtility::from_fixed(result(), vol_precision);
    }

    DEBUG_LOG("[DEBUG_VALUES] OBV calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<OBV *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<OBV *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<OBV *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &OBV::run;
    e.result_fn = &OBV::get_result;
    e.vector_fn = &OBV::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
