#include "benchmark.hpp"
#include "UDPReceiver.hpp"
#include "hft.hpp"
#include "utils/cpu_affinity.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <vector>

using namespace NetFeed;

void run_bench(const std::string &label, uint64_t iterations,
               const std::vector<HFTStorage::Packet> &pool) {
  std::cout << "\n>>> Starting Benchmark: " << label << " (" << iterations
            << " packets) <<<\n";
  Bench b(label, label + "_results.txt");
  b.start(iterations);

  size_t pool_size = pool.size();
  for (uint64_t i = 0; i < iterations; ++i) {
    const HFTStorage::Packet &pkt = pool[i % pool_size];
    b.tick();
    process_packet(pkt, pkt.size);
    b.tock();
  }
  b.end();
}

int main() {
  // 0. Pin to CPU 1 and set real-time priority (SCHED_FIFO)
  pin_thread_to_cpu(1);
  std::cout << "Thread pinned to CPU 1 with real-time priority.\n";

  // 1. Initialize HFT symbol access for symbol index 1
  std::vector<int64_t> precisions = {10, 10, 10, 10};
  HFT::symbolAccessArray[1].init(precisions, 4, true, 1);

  // Set storageTicks to a very large value to avoid disk I/O in the benchmark
  HFT::symbolAccessArray[1].storageTicks = 200000000;
  HFT::symbolAccessArray[1].symbol = 1;

  // 2. Pre-generate a pool of 1 million packets to reuse
  const size_t POOL_SIZE = 1000000;
  std::cout << "Pre-generating pool of " << POOL_SIZE << " fake packets..."
            << std::endl;
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

  // Warm-up
  for (int i = 0; i < 10000; ++i) {
    process_packet(packet_pool[i % POOL_SIZE], packet_pool[i % POOL_SIZE].size);
  }

  // 3. Run benchmarks at different scales
  run_bench("UDP_Bench_100K", 100000, packet_pool);
  run_bench("UDP_Bench_1M", 1000000, packet_pool);
  run_bench("UDP_Bench_10M", 10000000, packet_pool);
  run_bench("UDP_Bench_100M", 100000000, packet_pool);

  std::cout << "\nAll benchmarks complete.\n";

  return 0;
}
