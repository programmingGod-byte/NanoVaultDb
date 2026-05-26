
#pragma once

#include "global.hpp"
#include "hft.hpp"
#include "pool.hpp"
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

namespace IndicatorHandler {

namespace fs = std::filesystem;

// indicator registry

using IndicatorFactory = std::function<FastIndicators::IndicatorEntry(
    std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL> &, int64_t tick,
    HFT::TableColumn &, int64_t column, std::vector<std::string> &parameter)>;

std::unordered_map<std::string, IndicatorFactory> indicatorRegistry;

template <typename IndicatorType, std::size_t N>
void registerIndicator(
    std::unordered_map<std::string, IndicatorFactory> &registry,
    const std::string &name, IndicatorPool<IndicatorType, N> &pool) {
  registry[name] = [&pool, name](auto &symbolAccessArr, int64_t tick,
                                 HFT::TableColumn &table, int64_t col,
                                 std::vector<std::string> &params) {
    auto *indicator = pool.acquire(symbolAccessArr, tick, table, col);

    

    indicator->set_parameter(params);

    return indicator->create();
  };
}

void registerAllIndicators(
    std::unordered_map<std::string, IndicatorFactory> &registry) {
  registerIndicator<SMA, HFT::MAXHFTSYMBOL>(registry, "sma", smaPool);
  registerIndicator<OBI, HFT::MAXHFTSYMBOL>(registry, "obi", obiPool);
  registerIndicator<ADX, HFT::MAXHFTSYMBOL>(registry, "adx", adxPool);
  registerIndicator<ATR, HFT::MAXHFTSYMBOL>(registry, "atr", atrPool);
  registerIndicator<AwesomeOscillator, HFT::MAXHFTSYMBOL>(registry, "awesome_osc", awesomeOscillatorPool);
  registerIndicator<BollingerBands, HFT::MAXHFTSYMBOL>(registry, "bollinger", bollingerBandsPool);
  registerIndicator<CCI, HFT::MAXHFTSYMBOL>(registry, "cci", cciPool);
  registerIndicator<DonchianChannels, HFT::MAXHFTSYMBOL>(registry, "donchian_channels", donchianChannelsPool);
  registerIndicator<EMA, HFT::MAXHFTSYMBOL>(registry, "ema", emaPool);
  registerIndicator<Ichimoku, HFT::MAXHFTSYMBOL>(registry, "ichimoku", ichimokuPool);
  registerIndicator<KeltnerChannels, HFT::MAXHFTSYMBOL>(registry, "keltner_channels", keltnerChannelsPool);
  registerIndicator<MACD, HFT::MAXHFTSYMBOL>(registry, "macd", macdPool);
  registerIndicator<MFI, HFT::MAXHFTSYMBOL>(registry, "mfi", mfiPool);
  registerIndicator<OBV, HFT::MAXHFTSYMBOL>(registry, "obv", obvPool);
  registerIndicator<ParabolicSAR, HFT::MAXHFTSYMBOL>(registry, "parabolic_sar", parabolicSARPool);
  registerIndicator<PivotPoints, HFT::MAXHFTSYMBOL>(registry, "pivot_points", pivotPointsPool);
  registerIndicator<RSI, HFT::MAXHFTSYMBOL>(registry, "rsi", rsiPool);
  registerIndicator<Stochastic, HFT::MAXHFTSYMBOL>(registry, "stochastic", stochasticPool);
  registerIndicator<TRIX, HFT::MAXHFTSYMBOL>(registry, "trix", trixPool);
  registerIndicator<Volume, HFT::MAXHFTSYMBOL>(registry, "volume", volumePool);
  registerIndicator<VWAP, HFT::MAXHFTSYMBOL>(registry, "vwap", vwapPool);
  registerIndicator<WilliamsR, HFT::MAXHFTSYMBOL>(registry, "williams", williamsRPool);
}

void parseIndicators(std::unique_ptr<AddHftIndicatorStatement> &&statement) {
  // std::cout << "INDICATOR PARSING START\n";
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

  if (HFT::InitalStorage::checkIndicatorExists(baseName)) {
    error << std::format("indicator with base name  {}  already exist ",
                         baseName);
    // std::cout << error.str();
    throw std::runtime_error(error.str());
  }

  // std::cout << "CONTENT PRINTING\n";
  std::string content = (data.second);
  // std::cout << content << "\n";

  fs::path dir = "./fastindicator";

  fs::create_directories(dir);

  fs::path filePath = dir / (baseName + ".cpp");

  std::ofstream outFile(filePath);

  if (!outFile) {
    throw std::runtime_error("Failed to save the indicator  try again ");
  }

  outFile << content;

  outFile.close();
}

// parsing add indicator function
// ADD INDICATOR "INDICATOR_NAME" ("10") ON SYMBOL 2 COLUMN_NO  3 ticks 100;
// this form
void parsingAddIndicator(
    std::unique_ptr<AddIndicatorOnTableStatement> &&statement) {
  std::stringstream error;
  std::string indicatorName = MyUtility::to_lower(statement->indicator.first);
  auto [isIndicator, indicator] =
      HFT::InitalStorage::getIndicator(indicatorName);
  if (!isIndicator) {
    error << std::format("the indicator with name {} does not exist",
                         indicatorName);
    throw std::runtime_error(error.str());
  }
  std::string stringIndicatorPath = indicator.first;
  
  int64_t ticks = statement->ticks;
  int64_t columnNo = statement->column_no;
  int64_t symbol = statement->symbol;
  std::vector<std::string> params = std::move(statement->paramas);
  
  if (UNLIKELY(HFT::symbolAccessArray[symbol].symbol == -1)) {
    error << std::format("no table with this symbol exist\n");
    throw std::runtime_error(error.str());
  }
  if (UNLIKELY(HFT::symbolAccessArray[symbol].columnCount < columnNo)) {
    error << std::format("there is only {} columns but you give the no {}",
      HFT::symbolAccessArray[symbol].columnCount, columnNo);
      throw std::runtime_error(error.str());
    }
    
    
    auto * __restrict symbolData = &HFT::symbolAccessArray[symbol];
    int64_t indicatorIndex = symbolData->indicatorIndex;

    if(symbolData->indicatorsIndexStorage.find(indicatorName)==symbolData->indicatorsIndexStorage.end()){
      symbolData->indicatorsIndexStorage[indicatorName] = symbolData->indicatorIndex;
     
    }else{
      
      error << std::format("the indiator with name {} already exist",indicatorName);
      throw std::runtime_error(error.str());
    }

    symbolData->indicatorIndex++;

    if(symbolData->indicatorIndex >= HFT::MAX_NO_OF_INDICATORS){
      throw std::runtime_error("no of indicator exceeds the limit");
    }

   /* std::cout << std::format("the path is {} index is {} ticks is {} columnNo {} "
                           "symbol is {} index is {}",
                           stringIndicatorPath, indicatorIndex, ticks, columnNo,
                           symbol, indicatorIndex); */
  auto it = indicatorRegistry.find(indicatorName);
  if (it == indicatorRegistry.end()) {
    throw std::runtime_error("Indicator not registered: " + indicatorName);
  }

  auto entry = it->second(HFT::symbolAccessArray, ticks,
                          HFT::symbolAccessArray[symbol], columnNo, params);

  HFT::symbolAccessArray[symbol].indicators[indicatorIndex] = entry;
  // std::cout<<"\nIndicator added \n";
}



std::string parsingFetchIndicatorStatement(std::unique_ptr<FetchIndicatorStatement>&&statement){
  std::stringstream e;
  if(UNLIKELY(statement->symbol >= HFT::MAXHFTSYMBOL)){
    e << std::format("the symbol {} is greater than the max limit \n",statement->symbol,HFT::MAXHFTSYMBOL);
    throw std::runtime_error(e.str());
  }
  if(UNLIKELY(HFT::symbolAccessArray[statement->symbol].symbol == -1)){
    e << std::format("there is no table registerd with symbol {} \n",statement->symbol);
    throw std::runtime_error(e.str());
  }

  HFT::TableColumn * __restrict entry = &HFT::symbolAccessArray[statement->symbol];

 e << '{';

for (std::unordered_map<std::string, int64_t>::iterator it = entry->indicatorsIndexStorage.begin();
     it != entry->indicatorsIndexStorage.end(); ++it) {
    
    e << "\"" << it->first << "\": " << it->second;

    if (std::next(it) != entry->indicatorsIndexStorage.end()) {
        e << ", ";
    }
}

e << '}';

return e.str();
}



}; // namespace IndicatorHandler
