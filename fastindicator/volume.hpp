#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) Volume {

private:
  int64_t tick = 0;
  int64_t count = 0;
  int64_t vol_col = 4;
  std::vector<int> usedColumns;

  std::vector<double> values{-1.0};
  int64_t current_vol = 0;

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  Volume(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN) {
      vol_col = column_to_use;
    } else if (usedColumns.size() >= 1 && usedColumns[0] > 0) {
      vol_col = usedColumns[0];
    } else {
      vol_col = 5; // Default volume column for btc_ohlc
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    // Volume needs no parameters
  }

  inline int64_t result() const {
    return current_vol;
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (vol_col >= HFT::MAXCOLUMN) return;

    current_vol = *tableColumn->history[vol_col].latest_ptr();

    if (precision) {
      int64_t current_precision = precision[vol_col];
      values[0] = MyUtility::from_fixed(result(), current_precision);
    }

    DEBUG_LOG("[DEBUG_VALUES] Volume calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<Volume *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<Volume *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<Volume *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &Volume::run;
    e.result_fn = &Volume::get_result;
    e.vector_fn = &Volume::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
