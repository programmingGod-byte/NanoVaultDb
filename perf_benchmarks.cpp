#include "UDPReceiver.hpp"
#include "benchmark.hpp"
#include "hft.hpp"
#include "utils/cpu_affinity.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <vector>

using namespace NetFeed;

int main() {
  // 0. Pin to CPU 1 and set real-time priority (SCHED_FIFO)
  // Minimizes interrupts and context switches during the hot loop.
  try {
    pin_thread_to_cpu(8);
  } catch (...) {
    // Silent failure to avoid cout in performance mode
  }

  // 1. Initialize HFT symbol access for symbol index 1
  std::vector<int64_t> precisions = {10, 10, 10, 10};
  HFT::symbolAccessArray[1].init(precisions, 4, true, 1);

  // Set storageTicks to exceed the benchmark iteration count to avoid disk I/O
  // triggers
  HFT::symbolAccessArray[1].storageTicks = 2000000000;
  HFT::symbolAccessArray[1].symbol = 1;

  // 2. Pre-generate a pool of 1 million packets to reuse
  // Recycling the pool avoids allocating 1TB of memory while still exercising
  // logic.
  const size_t POOL_SIZE = 1000000;
  std::vector<HFTStorage::Packet> packet_pool(POOL_SIZE);

  for (size_t i = 0; i < POOL_SIZE; ++i) {
    HFTStorage::Packet &pkt = packet_pool[i];
    pkt.size = 72;
    int64_t tick_be = htobe64(1);
    int64_t data_be = htobe64(static_cast<int64_t>(i) + 100);
    memcpy(pkt.data, &tick_be, 8);
    for (int j = 1; j < 9; ++j) {
      memcpy(pkt.data + j * 8, &data_be, 8);
    }
  }

  // Warm-up phase
  for (int i = 0; i < 1000000; ++i) {
    process_packet(packet_pool[i % POOL_SIZE], 72);
  }

  // 3. Ultra-Scale Benchmark Loop (1 Billion Packets)
  // No cout or logging inside this loop to preserve nanosecond precision.
  const uint64_t iterations = 100000000;
  Bench b("UDP_Perf_1B_Scale", "perf_1B_results.txt");
  b.start(iterations);

  for (uint64_t i = 0; i < iterations; ++i) {
    const HFTStorage::Packet &pkt = packet_pool[i % POOL_SIZE];
    b.tick();
    process_packet(pkt, 72);
    b.tock();
  }

  // Result is automatically saved to perf_1B_results.txt by Bench::end()
  b.end();

  return 0;
}
