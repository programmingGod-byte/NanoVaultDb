#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) TRIX {

private:
  int64_t prev_ema1 = -1;
  int64_t prev_ema2 = -1;
  int64_t prev_ema3 = -1;
  int64_t prev_ema3_last = -1;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 15;
  int64_t filled = 0;
  
  int64_t close_col = 1;
  std::vector<int> usedColumns;

  // values[0] = TRIX
  std::vector<double> values{-1.0};

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  TRIX(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->usedColumns = usedColumn;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;

    if (column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN) {
      close_col = column_to_use;
    } else if (usedColumns.size() >= 1 && usedColumns[0] > 0) {
      close_col = usedColumns[0];
    } else {
      close_col = 2;
    }
  }

  void set_parameter(const std::vector<std::string> &params) {
    if (params.size() > 0) {
      auto [ptr, ec] = std::from_chars(params[0].data(), params[0].data() + params[0].size(), window);
    }
    this->prev_ema1 = -1;
    this->prev_ema2 = -1;
    this->prev_ema3 = -1;
    this->prev_ema3_last = -1;
    this->filled = 0;
  }

  inline int64_t result() const { return 0; }
  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    count++;
    if (count != tick) return;
    count = 0;

    if (close_col >= HFT::MAXCOLUMN) return;

    int32_t head = tableColumn->history[close_col].head;
    int64_t current_close = tableColumn->history[close_col].get(head);

    if (UNLIKELY(prev_ema1 == -1)) {
      prev_ema1 = current_close;
      prev_ema2 = current_close;
      prev_ema3 = current_close;
      prev_ema3_last = current_close;
      values[0] = 0.0;
      return;
    }

    prev_ema3_last = prev_ema3;

    prev_ema1 = (current_close * 2 + prev_ema1 * (window - 1)) / (window + 1);
    prev_ema2 = (prev_ema1 * 2 + prev_ema2 * (window - 1)) / (window + 1);
    prev_ema3 = (prev_ema2 * 2 + prev_ema3 * (window - 1)) / (window + 1);

    if (prev_ema3_last != 0) {
      double ema3_double = static_cast<double>(prev_ema3);
      double ema3_last_double = static_cast<double>(prev_ema3_last);
      values[0] = (ema3_double - ema3_last_double) / std::abs(ema3_last_double) * 10000.0; // Scaled for readability
    }

    DEBUG_LOG("[DEBUG_VALUES] TRIX calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<TRIX *>(p)->on_tick(); }
  static int64_t get_result(void *p) { return static_cast<TRIX *>(p)->result(); }
  static std::vector<double> *get_double_result(void *p) { return &static_cast<TRIX *>(p)->vector_result(); }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &TRIX::run;
    e.result_fn = &TRIX::get_result;
    e.vector_fn = &TRIX::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
