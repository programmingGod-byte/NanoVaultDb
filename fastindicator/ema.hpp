#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) EMA {

private:
  int64_t prev_ema = -1;
  int64_t sum = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14; // Default EMA window is 14
  int64_t filled = 0;
  int64_t column_to_use = -1;
  std::vector<int> usedColumns;

  // values[0] = EMA double value
  std::vector<double> values{-1.0};

  HFT::ColumnRing *columnRing = nullptr;
  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  EMA(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;

    if (LIKELY(column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN))
      this->column_to_use = column_to_use;
    else if (usedColumns.size() > 0 && usedColumns[0] > 0)
      this->column_to_use = usedColumns[0];
    else
      this->column_to_use = 2; // Default to close price column

    this->tableColumn = &tableColumn;

    if (LIKELY(this->column_to_use >= 0 && this->column_to_use < HFT::MAXCOLUMN))
      this->columnRing = &tableColumn.history[this->column_to_use];
    else
      this->columnRing = nullptr;

    this->precision = tableColumn.precisions;
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), window);
      if (ec != std::errc() || window <= 0) throw std::runtime_error("Invalid EMA window parameter");
    }

    this->prev_ema = -1;
    this->sum = 0;
    this->filled = 0;

    DEBUG_LOG("[EMA] Window set to: " << this->window);
  }

  inline int64_t result() const {
    if (filled < window && filled > 0) return sum / filled;
    return prev_ema == -1 ? 0 : prev_ema;
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    if (!columnRing) return;
    
    count++;
    if (count != tick) return;
    count = 0;

    const int64_t latest = *columnRing->latest_ptr();

    if (filled < window) {
      sum += latest;
      filled++;
      if (filled == window) {
        prev_ema = sum / window;
      }
    } else {
      // EMA_t = (Value_t * 2 + EMA_{t-1} * (N - 1)) / (N + 1)
      prev_ema = (latest * 2 + prev_ema * (window - 1)) / (window + 1);
    }

    if (precision && column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN) {
      int64_t current_precision = precision[column_to_use];
      values[0] = MyUtility::from_fixed(result(), current_precision);
    }

    DEBUG_LOG("[DEBUG_VALUES] EMA calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<EMA *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<EMA *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<EMA *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &EMA::run;
    e.result_fn = &EMA::get_result;
    e.vector_fn = &EMA::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
