#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) BollingerBands {

private:
  __int128_t sum = 0;
  __int128_t sum_sq = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 20; // Default Bollinger Bands window is 20
  double multiplier = 2.0; // Default std dev multiplier
  int64_t filled = 0;
  int64_t column_to_use = -1;
  std::vector<int> usedColumns;

  // values[0] = Upper Band, values[1] = Middle Band (SMA), values[2] = Lower Band
  std::vector<double> values{-1.0, -1.0, -1.0};

  HFT::ColumnRing *columnRing = nullptr;
  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  BollingerBands(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
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
      if (ec != std::errc() || window <= 0) throw std::runtime_error("Invalid Bollinger window parameter");
    }
    if (params.size() > 1) {
      // Parse double multiplier
      multiplier = std::stod(params[1]);
    }

    this->sum = 0;
    this->sum_sq = 0;
    this->filled = 0;

    DEBUG_LOG("[BollingerBands] Window set to: " << this->window << " Multiplier: " << this->multiplier);
  }

  inline int64_t result() const {
    if (filled == 0) return 0;
    return static_cast<int64_t>(sum / (filled < window ? filled : window));
  }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    if (!columnRing) return;
    
    count++;
    if (count != tick) return;
    count = 0;

    const int64_t latest = *columnRing->latest_ptr();
    int32_t head = columnRing->head;
    int32_t old_idx = (head - window - 1) & HFT::MAXRINGMASK;

    sum += latest;
    sum_sq += static_cast<__int128_t>(latest) * latest;

    if (filled < window) {
      filled++;
    } else {
      int64_t old_val = columnRing->get(old_idx);
      sum -= old_val;
      sum_sq -= static_cast<__int128_t>(old_val) * old_val;
    }

    if (precision && column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN && filled > 0) {
      int64_t current_precision = precision[column_to_use];
      
      int64_t current_window = filled < window ? filled : window;
      int64_t mean_fixed = static_cast<int64_t>(sum / current_window);
      double mean_double = MyUtility::from_fixed(mean_fixed, current_precision);
      
      double p10 = MyUtility::POW10[current_precision];
      double scale_sq = p10 * p10;
      double sum_sq_double = static_cast<double>(sum_sq) / scale_sq;
      
      double variance = (sum_sq_double / current_window) - (mean_double * mean_double);
      double std_dev = std::sqrt(variance > 0 ? variance : 0);

      values[0] = mean_double + (multiplier * std_dev); // Upper Band
      values[1] = mean_double;                          // Middle Band
      values[2] = mean_double - (multiplier * std_dev); // Lower Band
    }

    DEBUG_LOG("[DEBUG_VALUES] BollingerBands calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<BollingerBands *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<BollingerBands *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<BollingerBands *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &BollingerBands::run;
    e.result_fn = &BollingerBands::get_result;
    e.vector_fn = &BollingerBands::get_double_result;
    e.indicatorIndex = -1;
    return e;
  }
};
