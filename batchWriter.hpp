#pragma once
#include "databaseSchemaReader.hpp"
#include "global.hpp"
#include "hft.hpp"
#include "json.hpp"
#include "utility.hpp"
#include "utils/types.hpp"
#include <charconv>
#include <cstdint>
#include <fcntl.h>
#include <format>
#include <liburing.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "io_uring_queue.hpp"

void IoUringQueue::wait_all(size_t expected, size_t expected_bytes) {
    for (size_t i = 0; i < expected; i++) {
        io_uring_cqe *cqe;
        int r = io_uring_wait_cqe(&ring_, &cqe);
        if (r < 0) {
            HFT_DEBUG_FILE("batch.txt", "wait_cqe failed with error: " + std::to_string(r));
            throw std::runtime_error("wait_cqe failed");
        }

        HFT_DEBUG_FILE("batch.txt", "CQE received, res (bytes written): " + std::to_string(cqe->res));

        if (cqe->res < 0) {
            HFT_DEBUG_FILE("batch.txt", "write failed with error code: " + std::to_string(cqe->res));
            throw std::runtime_error("write failed");
        }

        if (expected_bytes > 0 &&
            static_cast<size_t>(cqe->res) != expected_bytes) {
            HFT_DEBUG_FILE("batch.txt", "partial write detected: " + std::to_string(cqe->res) + " bytes");
            throw std::runtime_error("partial write detected");
        }

        io_uring_cqe_seen(&ring_, cqe);
    }
}

void IoUringQueue::submit_and_drain() {
    int ret = io_uring_submit(&ring_);
    if (ret < 0)
        throw std::runtime_error("submit failed");
    wait_all(static_cast<size_t>(ret));
}


void IoUringQueue::batchWrite(const std::vector<std::string> &data) {
    if (data.empty())
        return;

    size_t submitted = 0;
    while (submitted < data.size()) {
        auto *sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            int ret = io_uring_submit(&ring_);
            if (ret < 0)
                throw std::runtime_error("submit failed");
            wait_all(static_cast<size_t>(ret));
            continue;
        }

        const std::string &s = data[submitted];
        io_uring_prep_write(sqe, fd_, s.data(), s.size(), offset_);
        offset_ += s.size();
        submitted++;
    }

    int ret = io_uring_submit(&ring_);
    if (ret < 0)
        throw std::runtime_error("submit failed");
    wait_all(static_cast<size_t>(ret));
}

namespace BatchWriter {

static constexpr int64_t batchWriterQueueSize = 1024;
struct alignas(CACHELINE) batchWriterPacket {
  int64_t symbol;
  std::vector<int64_t> data;
};

// SPSCQueue<batchWriterPacket, batchWriterQueueSize> batchWriterPacketQueue;

void parseEnableBatchStatement(std::unique_ptr<EnableStatement> &&statement) {
  std::string currentDb =
      dbDirectoryPath + "/" + currentDatabase + ".shivam" + ".db";
  std::cout << currentDb << "\n";
  if (!MyUtility::checkIfFileExist(currentDb)) {
    throw std::runtime_error(
        std::format("the file {} does not exist", currentDb));
  }

  std::shared_ptr<JSONParser> parser = std::make_shared<JSONParser>();

  if (!parser->loadFromFile(currentDb)) {
    std::cerr << "Failed to load file: " << currentDb << std::endl;
    throw std::runtime_error("Failed to load file: " + currentDb);
  }

  int ticks = statement->ticks;

  JSONParser::JSONValue rootValue = parser->getObject(0);
  JSONParser::JSONObject &rootObj =
      std::get<JSONParser::JSONObject>(rootValue.value);

  JSONParser::JSONArray &tablesArray =
      std::get<JSONParser::JSONArray>(rootObj["tables"].value);

  for (auto &tableVal : tablesArray) {
    JSONParser::JSONObject &table =
        std::get<JSONParser::JSONObject>(tableVal.value);

    std::string tableName = std::get<std::string>(table["name"].value);

    int64_t symbol = -1;
    auto symbolIt = table.find("symbol");
    if (symbolIt != table.end() &&
        !std::holds_alternative<std::nullptr_t>(symbolIt->second.value)) {
      if (std::holds_alternative<int>(symbolIt->second.value)) {
        symbol = std::get<int>(symbolIt->second.value);
      } else if (std::holds_alternative<std::string>(symbolIt->second.value)) {
        symbol = std::stoll(std::get<std::string>(symbolIt->second.value));
      } else if (std::holds_alternative<double>(symbolIt->second.value)) {
        symbol = static_cast<int64_t>(std::get<double>(symbolIt->second.value));
      } else {
        throw std::runtime_error("Invalid type for symbol in JSON");
      }
    }
    if (tableName == statement->tableName) {
      if (symbol != -1) {
        std::cout << "storage symbol and ticks is " << symbol << " " << ticks
                  << "\n";
        HFT::symbolAccessArray[symbol].storageTicks = ticks;
        auto it_f = batchWriterFileMap.find(symbol);
        if (it_f == batchWriterFileMap.end() || !it_f->second) {
          std::string indexFileName = tableDirectory + "/" + currentDatabase + "/" + tableName + ".data";
          std::cout << "[BATCH_WRITER] Initializing IoUringQueue for " << indexFileName << " (Symbol " << symbol << ")\n";
          batchWriterFileMap[symbol] = std::make_unique<IoUringQueue>(indexFileName);
        }
      }
      table["ticks"] = JSONParser::JSONValue(ticks);
      break;
    }
  }

  parser->removeObject(0);
  parser->appendValue(rootValue);
  parser->saveToFile(currentDb);
}

void parseDisableBatchStatement(std::unique_ptr<DisableStatement> &&statement){
  std::string currentDb =
      dbDirectoryPath + "/" + currentDatabase + ".shivam" + ".db";
  std::cout << currentDb << "\n";
  if (!MyUtility::checkIfFileExist(currentDb)) {
    throw std::runtime_error(
        std::format("the file {} does not exist", currentDb));
  }

  std::shared_ptr<JSONParser> parser = std::make_shared<JSONParser>();

  if (!parser->loadFromFile(currentDb)) {
    std::cerr << "Failed to load file: " << currentDb << std::endl;
    throw std::runtime_error("Failed to load file: " + currentDb);
  }

  JSONParser::JSONValue rootValue = parser->getObject(0);
  JSONParser::JSONObject &rootObj =
      std::get<JSONParser::JSONObject>(rootValue.value);

  JSONParser::JSONArray &tablesArray =
      std::get<JSONParser::JSONArray>(rootObj["tables"].value);
  for (auto &tableVal : tablesArray) {
    JSONParser::JSONObject &table =
        std::get<JSONParser::JSONObject>(tableVal.value);

    std::string tableName = std::get<std::string>(table["name"].value);
    if (tableName == statement->tableName) {
      int64_t symbol = -1;
      auto symbolIt = table.find("symbol");
      if (symbolIt != table.end() &&
          !std::holds_alternative<std::nullptr_t>(symbolIt->second.value)) {
        if (std::holds_alternative<int>(symbolIt->second.value)) {
          symbol = std::get<int>(symbolIt->second.value);
        } else if (std::holds_alternative<std::string>(symbolIt->second.value)) {
          symbol = std::stoll(std::get<std::string>(symbolIt->second.value));
        } else if (std::holds_alternative<double>(symbolIt->second.value)) {
          symbol = static_cast<int64_t>(std::get<double>(symbolIt->second.value));
        } else {
          throw std::runtime_error("Invalid type for symbol in JSON");
        }
      }

      if (symbol != -1) {
        HFT::symbolAccessArray[symbol].storageTicks = -1;
        batchWriterFileMap.erase(symbol);
      }
      table["ticks"] = JSONParser::JSONValue(-1);
      break;
    }
  }

  parser->removeObject(0);
  parser->appendValue(rootValue);
  parser->saveToFile(currentDb);
}

bool writeHFTDataToIndexFile(int symbol) {
  auto it = batchWriterFileMap.find(symbol);
  auto *__restrict entry = &HFT::symbolAccessArray[symbol];

  entry->count++;
  if (entry->count % 1000 == 0) {
      HFT_DEBUG_FILE("batch.txt", "writeHFTDataToIndexFile: symbol=" + std::to_string(symbol) + " count=" + std::to_string(entry->count) + " storageTicks=" + std::to_string(entry->storageTicks));
  }

  if (UNLIKELY(entry->count >= entry->storageTicks && entry->storageTicks > 0)) {
    entry->count = 0;
    if (it != batchWriterFileMap.end() && it->second) {
      std::vector data = HFT::symbolAccessArray[symbol].getWritingData();
      HFT_DEBUG_FILE("batch.txt", "Batch writing started. Data size: " + std::to_string(data.size()));
      it->second->batchWrite(data);
      return true;
    } else {
      HFT_DEBUG_FILE("batch.txt", "the symbol does not exist " + std::to_string(symbol));
    }
  }

  return false;
}

}; // namespace BatchWriter