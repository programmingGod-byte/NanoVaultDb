#pragma once
#include "../FastIndicators.hpp"
#include "../hft.hpp"
#include "../utility.hpp"
#include "../utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <vector>

class alignas(CACHELINE) RSI {

private:
  int64_t prev_val = -1;
  int64_t avg_gain = 0;
  int64_t avg_loss = 0;

  int64_t running_gain = 0;
  int64_t running_loss = 0;

  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 14; // Default RSI window is 14
  int64_t filled = 0;
  int64_t column_to_use = -1;
  int64_t rsi_value = 0;

  std::vector<double> values{-1.0};

  HFT::ColumnRing *columnRing = nullptr;
  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  RSI(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn, int64_t column_to_use = -1,
      std::vector<int> usedColumn = {0,0,0,0,0}) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;

    if (LIKELY(column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN))
      this->column_to_use = column_to_use;
    else if (usedColumn.size() > 0 && usedColumn[0] > 0)
      this->column_to_use = usedColumn[0];
    else
      this->column_to_use = 2;

    this->tableColumn = &tableColumn;

    if (LIKELY(this->column_to_use >= 0 &&
               this->column_to_use < HFT::MAXCOLUMN))
      this->columnRing = &tableColumn.history[this->column_to_use];
    else
      this->columnRing = nullptr;

    this->precision = tableColumn.precisions;
  }

  void set_parameter(const std::vector<std::string> &win) {
    std::string param = win[0];
    int64_t window = 14;

    auto [ptr, ec] =
        std::from_chars(param.data(), param.data() + param.size(), window);

    if (ec != std::errc() || window <= 0) {
      throw std::runtime_error("Invalid RSI parameter");
    }

    this->window = window;
    this->prev_val = -1;
    this->avg_gain = 0;
    this->avg_loss = 0;
    this->running_gain = 0;
    this->running_loss = 0;
    this->filled = 0;
    this->rsi_value = 0;

    DEBUG_LOG("[RSI] Window set to: " << this->window);
  }

  inline int64_t result() const { return rsi_value; }

  inline std::vector<double> &vector_result() noexcept { return values; }

  void on_tick() {
    DEBUG_LOG("on tick called");
    if (!columnRing)
      return;
    DEBUG_LOG("on tick called 1");
    count++;
    if (count != tick)
      return;
    DEBUG_LOG("on tick called 2");
    count = 0;

    const int64_t latest = *columnRing->latest_ptr();

    if (UNLIKELY(prev_val == -1)) {
      prev_val = latest;
      rsi_value =
          50 * (precision ? MyUtility::POW10[precision[column_to_use]] : 1);
      return;
    }

    int64_t change = latest - prev_val;
    int64_t gain = change > 0 ? change : 0;
    int64_t loss = change < 0 ? -change : 0;
    prev_val = latest;

    if (filled < window) {
      running_gain += gain;
      running_loss += loss;
      filled++;

      if (filled == window) {
        avg_gain = running_gain / window;
        avg_loss = running_loss / window;
      }
    } else {
      avg_gain = (avg_gain * (window - 1) + gain) / window;
      avg_loss = (avg_loss * (window - 1) + loss) / window;
    }

    if (filled == window) {
      int64_t total = avg_gain + avg_loss;
      if (total == 0) {
        rsi_value =
            50 * (precision ? MyUtility::POW10[precision[column_to_use]] : 1);
      } else {
        int64_t current_precision = precision ? precision[column_to_use] : 8;
        __int128_t scale_val = 100 * MyUtility::POW10[current_precision];
        rsi_value =
            static_cast<int64_t>(scale_val - (scale_val * avg_loss) / total);
      }
    } else {
      rsi_value =
          50 * (precision ? MyUtility::POW10[precision[column_to_use]] : 1);
    }

    // Update the values vector with latest RSI double value
    if (precision && column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN) {
      double rsi_double =
          MyUtility::from_fixed(result(), precision[column_to_use]);
      values[0] = rsi_double;
    }

    HFT_DEBUG_FILE("hello.txt",
                   std::format("latest is {} gain is {} loss is {} avg_gain is "
                               "{} avg_loss is {} rsi is {} \n",
                               latest, gain, loss, avg_gain, avg_loss,
                               rsi_value));

    DEBUG_LOG("[DEBUG_VALUES] RSI calculated | result(): " << result());
    for(size_t i=0; i<values.size(); i++) {
        DEBUG_LOG("    -> values[" << i << "] = " << values[i]);
    }
    DEBUG_LOG("----------------------");
  }

  static void run(void *p) { static_cast<RSI *>(p)->on_tick(); }

  static int64_t get_result(void *p) { return static_cast<RSI *>(p)->result(); }

  static std::vector<double> *get_double_result(void *p) {
    return &static_cast<RSI *>(p)->vector_result();
  }

  FastIndicators::IndicatorEntry create() {
    FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &RSI::run;
    e.result_fn = &RSI::get_result;
    e.vector_fn = &RSI::get_double_result;
    e.indicatorIndex = -1;

    return e;
  }
};
