#ifndef HFT_CODE
#define HFT_CODE

#include "utils/types.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include "utils/spsc.hpp"

namespace HFTStorage {
  // SPSCQueue<>;
  std::atomic<int64_t> dropped{0};
  static constexpr size_t PacketSize = 2048;
  static constexpr size_t PacketParserQueueSize = 1024;
  alignas(CACHELINE) struct Packet{
    alignas(CACHELINE)  char data[PacketSize];
    int64_t size;
  };
  
  SPSCQueue<Packet, PacketParserQueueSize> PacketParseQueue;

}


namespace HFT {

constexpr int64_t SCALINGFACTOR = 1e6;
constexpr int64_t MAXHFTSYMBOL = 100;
constexpr int64_t MAXCOLUMN = 16;
constexpr int64_t MAXRINGSIZE = 256;
constexpr int64_t MAXRINGMASK = MAXRINGSIZE - 1;
constexpr int64_t OrderBookSize = 4;
constexpr int64_t MAX_NO_OF_INDICATORS = 128;

namespace InitalStorage {
  std::unordered_map<std::string, std::pair<std::string, int64_t>> Indicators;
  bool initialIndicatorLoad() {
    // std::cout<<"INITIAL INDICATOR LOAD\n";
    namespace fs = std::filesystem;

    fs::path relativePath = "./fastindicator";

    if (!fs::exists(relativePath) || !fs::is_directory(relativePath)) {
        return false;
    }

    int64_t index=  0;
    for (const auto& entry : fs::directory_iterator(relativePath)) {
        if (entry.is_regular_file()) {
            fs::path filePath = entry.path();

            std::string baseName = filePath.stem().string();

            std::string absolutePath = fs::absolute(filePath).string();
            // std::cout<<baseName<<"\n";
            Indicators[baseName] = {absolutePath,index};
            index++;
        }
    }

    return true;
  }

  bool checkIndicatorExists(std::string &s){
    if(LIKELY(Indicators.find(s)!=Indicators.end())) return false;
    return true;
  }
};

struct alignas(64) ColumnRing {

  alignas(64) int64_t data[MAXRINGSIZE];
  int32_t head = 0;

  inline void push(int64_t v) {
    data[head] = v;
    head = (head + 1) & MAXRINGMASK;
  }

  inline int64_t get(int32_t idx) const { return data[idx & MAXRINGMASK]; }

  inline int64_t *latest_ptr() { return &data[(head - 1) & MAXRINGMASK]; }
};

struct alignas(64) TableColumn {

  int64_t precisions[MAXCOLUMN];

  ColumnRing history[MAXCOLUMN];

  // ask order ask quantity bid order bid quantity
  int64_t topOrderBookPrecision = 10;
  alignas(64) int64_t topOrderBook[OrderBookSize];

  int64_t isTopOrderBook = false;

  int32_t columnCount = 0;
  int32_t symbol = -1;


  ColumnRing& operator [](int64_t index){
    return this->history[index];
  }

  void init(std::vector<int64_t>&precisions,int cols, bool isBook = false, int sym = -1) {
    std::cout << "HFT symbol initialized  " << " " << cols << " " << isBook
              << " " << sym << "\n";
    columnCount = cols;
    this->symbol = sym;
    isTopOrderBook = isBook;

    for (int i = 0; i < cols; i++) {
      // values[i] = -1;
      this->precisions[i] = precisions[i];
    }

    for (int i = 0; i < OrderBookSize; i++)
      topOrderBook[i] = -1;
  }

  inline void pushHistory(int col, int64_t v) { history[col].push(v); }
};

alignas(64) std::array<TableColumn, MAXHFTSYMBOL> symbolAccessArray;

// inline void initTables(int columnsPerSymbol) {
//   for (int i = 0; i < MAXHFTSYMBOL; i++)
//     symbolAccessArray[i].init(columnsPerSymbol, false, i);
// }

} // namespace HFT









#endif