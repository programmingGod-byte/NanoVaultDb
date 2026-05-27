
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
  // std::cout << "strategy PARSING START\n";
  std::string file_path = statement->file_path;
  std::stringstream error;
  file_path.erase(std::remove(file_path.begin(), file_path.end(), '\r'),
                  file_path.end());
  if (UNLIKELY(!MyUtility::checkIfFileExist(file_path))) {
    error << std::format("the file {} not exist ", file_path);
    // std::cout << error.str();
    throw std::runtime_error(error.str());
  }

  std::string baseName = MyUtility::extractBaseName(file_path);
  std::pair<bool, std::string> data = MyUtility::readAFile(file_path);
  if (!data.first) {
    error << std::format("error while opening the  file {}  ", file_path);
    // std::cout << error.str();
    throw std::runtime_error(error.str());
  }

  if (HFT::InitalStorage::checkStrategyExits(baseName)) {
    error << std::format("strategy with base name  {}  already exist ",
                         baseName);
    // std::cout << error.str();
    throw std::runtime_error(error.str());
  }

  // std::cout << "CONTENT PRINTING\n";
  std::string content = (data.second);
  // std::cout << content << "\n";

  fs::path dir = "./faststrategy";

  fs::create_directories(dir);

  fs::path filePath = dir / (baseName + ".hpp");

  std::ofstream outFile(filePath);

  if (!outFile) {
    throw std::runtime_error("Failed to save the strategy  try again ");
  }

  outFile << content;

  outFile.close();
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