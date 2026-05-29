
#pragma once

#include "FastStrategy.hpp"
#include "global.hpp"
#include "hft.hpp"
#include "pool.hpp"
#include "strategyPool.hpp"
#include "strategyinclude.hpp"
#include "utility.hpp"
#include "utils/types.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace StrategyHandler {
namespace fs = std::filesystem;

using StrategyFactory = std::function<FastStrategy::StrategyEntry(
    std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &, int64_t tick,
    HFT::TableColumn &, std::vector<std::string> &parameter)>;

std::unordered_map<std::string, StrategyFactory> strategyRegistry;

template <typename StrategyType, std::size_t N>
void registerStrategy(
    std::unordered_map<std::string, StrategyFactory> &registry,
    const std::string &name, StrategyPool<StrategyType, N> &pool) {
  registry[name] = [&pool, name](auto &symbolAccessArr, int64_t tick,
                                 HFT::TableColumn &table,
                                 std::vector<std::string> &params) {
    auto *strategy = pool.acquire(symbolAccessArr, tick, table);

    strategy->set_parameter(params);

    return strategy->create();
  };
}

void registerAllStrategy(
    std::unordered_map<std::string, StrategyFactory> &registry) {
        // REGISTRATIONS_START
  registerStrategy<AGAIN, HFT::MAXHFTSYMBOL>(registry, "again", againPool);
  registerStrategy<BASIC, HFT::MAXHFTSYMBOL>(registry, "basic", basicPool);
  registerStrategy<BASIC3, HFT::MAXHFTSYMBOL>(registry, "basic3", basicPool3);
  // REGISTRATIONS_END
}

void parseStrategy(std::unique_ptr<AddHftStrategyStatement> &&statement) {
  std::string file_path = statement->file_path;
  std::stringstream error;
  file_path.erase(std::remove(file_path.begin(), file_path.end(), '\r'),
                  file_path.end());
  if (UNLIKELY(!MyUtility::checkIfFileExist(file_path))) {
    error << std::format("the file {} not exist ", file_path);
    throw std::runtime_error(error.str());
  }

  JSONParser parser;
  if (!parser.loadFromFile(file_path)) {
    throw std::runtime_error("Failed to load JSON file using JSON parser: " + file_path);
  }

  auto printJSONValue = [](const auto& self, const JSONParser::JSONValue& val, int indent = 0) -> void {
    std::string indentStr(indent, ' ');
    std::visit([&](const auto& v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, std::nullptr_t>) {
        std::cout << "null";
      } else if constexpr (std::is_same_v<T, bool>) {
        std::cout << (v ? "true" : "false");
      } else if constexpr (std::is_same_v<T, int>) {
        std::cout << v;
      } else if constexpr (std::is_same_v<T, double>) {
        std::cout << v;
      } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "\"" << v << "\"";
      } else if constexpr (std::is_same_v<T, JSONParser::JSONArray>) {
        std::cout << "[\n";
        for (size_t i = 0; i < v.size(); ++i) {
          std::cout << indentStr << "  ";
          self(self, v[i], indent + 2);
          if (i + 1 < v.size()) std::cout << ",";
          std::cout << "\n";
        }
        std::cout << indentStr << "]";
      } else if constexpr (std::is_same_v<T, JSONParser::JSONObject>) {
        std::cout << "{\n";
        size_t count = 0;
        for (const auto& [key, value] : v) {
          std::cout << indentStr << "  \"" << key << "\": ";
          self(self, value, indent + 2);
          if (++count < v.size()) std::cout << ",";
          std::cout << "\n";
        }
        std::cout << indentStr << "}";
      }
    }, val.value);
  };
  if (parser.size() == 0) {
    throw std::runtime_error("JSON file is empty or invalid");
  }

  const auto& root = parser.getObject(0);
  if (!std::holds_alternative<JSONParser::JSONObject>(root.value)) {
    throw std::runtime_error("JSON root must be an object");
  }
  const auto& obj = std::get<JSONParser::JSONObject>(root.value);

  auto getStr = [&](const std::string& key) -> std::string {
    auto it = obj.find(key);
    if (it == obj.end())
      throw std::runtime_error("JSON missing required field: " + key);
    if (!std::holds_alternative<std::string>(it->second.value))
      throw std::runtime_error("JSON field '" + key + "' must be a string");
    return std::get<std::string>(it->second.value);
  };
  std::string indicators_expr = getStr("result");
  std::string stratName  = MyUtility::to_lower(getStr("name"));
  std::string opStr      = getStr("operator");
  int64_t     threshold  = std::stoll(getStr("threshold"));
  int64_t     symIdx     = std::stoll(getStr("symbol"));

  if (HFT::InitalStorage::checkStrategyExits(stratName)) {
    throw std::runtime_error(
        std::format("strategy with name '{}' already exists", stratName));
  }
  if (symIdx < 0 || symIdx >= HFT::MAXHFTSYMBOL ||
      HFT::symbolAccessArray[symIdx].symbol == -1) {
    throw std::runtime_error(
        std::format("JSON strategy: symbol {} does not exist", symIdx));
  }
  int64_t availableIndicators = HFT::symbolAccessArray[symIdx].indicatorIndex;
  for (char c : indicators_expr) {
    if (c >= '0' && c <= '9') {
      int id = c - '0';
      if (id >= availableIndicators) {
        throw std::runtime_error(
            std::format("JSON strategy '{}': indicator index {} out of range "
                        "(symbol {} has {} indicators)",
                        stratName, id, symIdx, availableIndicators));
      }
    }
  }

  struct JSONStrategy {
    int64_t tick      = 0;
    int64_t count     = 0;
    int64_t threshold = 0;
    int64_t symIdx    = 0;
    std::string op;
    std::string resultExpr;  
    HFT::TableColumn* tableColumn = nullptr;

    inline bool result() {
      count++;
      if (LIKELY(count != tick)) return false;
      count = 0;

      int64_t val = 0;
      bool pos = true;   
      for (size_t i = 0; i < resultExpr.size(); i++) {
        char c = resultExpr[i];
        if (c == '+') {pos = true; continue; }
        if (c == '-') {pos = false; continue; }
        if (c >= '0' && c <= '9') {
          int id = c - '0';
          // bounds already validated at registration time — safe to read
          int64_t ival = tableColumn->indicators[id].result_fn(
                             tableColumn->indicators[id].ptr);
          if (pos) val += ival;
          else val -= ival;
        }
      }

      if (op == "greater-than") return val > threshold;
      if (op == "less-than")    return val < threshold;
      if (op == "equal")        return val == threshold;
      return false;
    }

    void on_tick() {}

    static void run_fn(void* p) { static_cast<JSONStrategy*>(p)->on_tick(); }
    static bool res_fn(void* p) { return static_cast<JSONStrategy*>(p)->result(); }

    FastStrategy::StrategyEntry create() {
      FastStrategy::StrategyEntry e;
      e.checked       = 1;
      e.ptr           = this;
      e.fn            = &JSONStrategy::run_fn;
      e.result_fn     = &JSONStrategy::res_fn;
      e.strategyIndex = -1;
      return e;
    }
  };

  strategyRegistry[stratName] = [threshold, opStr, indicators_expr, symIdx](
      std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL>& /*symbolAccessArr*/,
      int64_t tick,
      HFT::TableColumn& table,
      std::vector<std::string>& /*params*/) -> FastStrategy::StrategyEntry {

    auto* js        = new JSONStrategy();
    js->tick        = tick;
    js->count       = 0;
    js->threshold   = threshold;
    js->op          = opStr;
    js->resultExpr  = indicators_expr;
    js->symIdx      = symIdx;
    js->tableColumn = &table;
    return js->create();
  };

  HFT::InitalStorage::Strategy[stratName] = {file_path, 0};

  std::cout << std::format(
      "Runtime JSON strategy '{}' registered (op={} threshold={} symbol={})\n",
      stratName, opStr, threshold, symIdx);
}

void parsingAddStrategy(
    std::unique_ptr<AddStrategyOnTableStatement> &&statement) {
  std::stringstream error;
  std::string strategyName = MyUtility::to_lower(statement->strategy.first);
  auto [isStrategy, strategy] = HFT::InitalStorage::getStrategy(strategyName);
  if (!isStrategy) {
    error << std::format("the strategy with name {} does not exist",
                         strategyName);
    throw std::runtime_error(error.str());
  }
  std::string stringStrategyPath = strategy.first;

  int64_t ticks = statement->ticks;
  int64_t symbol = statement->symbol;
  std::vector<std::string> params = std::move(statement->paramas);

  if (UNLIKELY(HFT::symbolAccessArray[symbol].symbol == -1)) {
    error << std::format("no table with this symbol exist\n");
    throw std::runtime_error(error.str());
  }

      auto * __restrict symbolData = &HFT::symbolAccessArray[symbol];
    int64_t strategyIndex = symbolData->strategyIndex;

    if(symbolData->strategysIndexStorage.find(strategyName)==symbolData->strategysIndexStorage.end()){
      symbolData->strategysIndexStorage[strategyName] = symbolData->strategyIndex;
      symbolData->indexToStrategy[symbolData->strategyIndex] = strategyName;
    }else{
      
      error << std::format("the indiator with name {} already exist",strategyName);
      throw std::runtime_error(error.str());
    }

    symbolData->strategyIndex++;

    if(symbolData->strategyIndex >= HFT::MAX_NO_OF_INDICATORS){
      throw std::runtime_error("no of indicator exceeds the limit");
    }


     /* std::cout << std::format("the path is {} index is {} ticks is {}  "
                           "symbol is {} index is {}",
                           stringStrategyPath, strategyIndex, ticks,
                           symbol, strategyIndex); */
  auto it = strategyRegistry.find(strategyName);

   if (it == strategyRegistry.end()) {
    throw std::runtime_error("strategy not registered: " + strategyName);
  }

    auto entry = it->second(HFT::symbolAccessArray, ticks,
                          HFT::symbolAccessArray[symbol], params);

  HFT::symbolAccessArray[symbol].strategys[strategyIndex] = entry;
  // std::cout<<"\n strategy added \n";

  
};

}; // namespace StrategyHandler