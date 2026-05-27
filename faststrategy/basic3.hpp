#pragma once
#include "../hft.hpp"
#include "../utils//types.hpp"
#include <charconv>
#include <cstdint>
#include "../utility.hpp"
#include "format"
#include "../FastStrategy.hpp"
#include <iostream>

class alignas(CACHELINE) BASIC3 {

private:
  int64_t tick = 0;
  int64_t count = 0;
  int64_t window = 1;

  HFT::TableColumn *tableColumn = nullptr;
  std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> *symbolAccessArr;
  const int64_t *precision = nullptr;

public:
  BASIC3(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &symbolAccessArr,
      int64_t tick, HFT::TableColumn &tableColumn) {

    this->symbolAccessArr = &symbolAccessArr;
    this->tick = tick;
    this->count = 0;
    this->tableColumn = &tableColumn;
    this->precision = tableColumn.precisions;
  }

  void set_parameter(const std::vector<std::string> &win) {
    std::string param = win[0];
    int64_t window = 1;

    auto [ptr, ec] = std::from_chars(param.data(), param.data() + param.size(), window);

    if (ec != std::errc() || window <= 0) {
      throw std::runtime_error("Invalid SMA parameter");
    }

    this->window = window;

    std::cout << "basic strategy window set to: " << this->window << "\n";
  }

  inline bool result()  {
        count++;
        
        if(LIKELY(count!=tick)) {
          
          return false;
        }
        count = 0;
        auto const * __restrict entry = &this->tableColumn->indicators[1];
        int64_t val = entry->result_fn(entry->ptr);
        HFT_DEBUG_FILE("basic.txt", std::format("running val is {} is true {}",val,val > this->window));
        return val > this->window;
  }

  void on_tick() {
    
  }

  static void run(void *p) { static_cast<BASIC3 *>(p)->on_tick(); }
    static bool get_result(void *p) {
    return static_cast<BASIC3 *>(p)->result();
  }
  FastStrategy::StrategyEntry create() {
    FastStrategy::StrategyEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &BASIC3::run;
    e.result_fn = &BASIC3::get_result;
    e.strategyIndex = -1;
    return e;
  }
};