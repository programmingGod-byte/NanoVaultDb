#ifndef __UDP_RECEIVER
#define __UDP_RECEIVER


#include "utils/types.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "hft.hpp"

namespace NetFeed {

constexpr int PORT = 9090;

FORCE_INLINE int64_t read_be64(const char *p) {
    int64_t val;
    memcpy(&val, p, sizeof(val));
    return be64toh(val);
}
COLD void packet_error() {
  std::cerr << "Invalid packet\n";
}
int64_t count = 0;

HOT void process_packet(const HFTStorage::Packet& packet, ssize_t n) {
    const char * __restrict__ buffer = packet.data;
    count++;

    const int64_t tick = read_be64(buffer);
    if (UNLIKELY(tick < 0 || tick >= HFT::MAXHFTSYMBOL)) {
      std::cout << "Invalid tick index: " << tick << "\n";
      return;
}


    std::cout << "tick = " << tick << "\n";

    auto *__restrict entry = &HFT::symbolAccessArray[tick];

    std::cout << "symbol = " << entry->symbol << "\n";

    if (UNLIKELY(entry->symbol == -1)) {
        std::cout << "ERROR: invalid symbol\n";
        return;
    }

    const int64_t columnCount = entry->columnCount;
    const int64_t isTop = entry->isTopOrderBook;

    std::cout << "columnCount = " << columnCount << "\n";
    std::cout << "isTop = " << isTop << "\n";

    const int64_t expected = (columnCount + 1 + (isTop << 2)) << 3;

    std::cout << "packet size n = " << n << "\n";
    std::cout << "expected size = " << expected << "\n";

    if (UNLIKELY(n != expected)) {
        std::cout << "ERROR: packet size mismatch\n";
        return;
    }

    const char *__restrict ptr = buffer + 8;

    std::cout << "starting history loop\n";

    for (int64_t i = 0; i < columnCount; ++i) {
        int64_t value = read_be64(ptr);
        std::cout << "history[" << i << "] = " << value << "\n";

        entry->pushHistory(i, value);
        ptr += 8;
    }

    if (LIKELY(isTop)) {
        std::cout << "reading topOrderBook\n";

        entry->topOrderBook[0] = read_be64(ptr);
        entry->topOrderBook[1] = read_be64(ptr + 8);
        entry->topOrderBook[2] = read_be64(ptr + 16);
        entry->topOrderBook[3] = read_be64(ptr + 24);

        std::cout << "topOrderBook: "
                  << entry->topOrderBook[0] << " "
                  << entry->topOrderBook[1] << " "
                  << entry->topOrderBook[2] << " "
                  << entry->topOrderBook[3] << "\n";
    }

    std::cout << "ticks " << entry->symbol
              << " latest price "
              << *entry->history[entry->symbol].latest_ptr()
              << "\n";
}

void run_receiver() {

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    return;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return;
  }

  alignas(CACHELINE) char buffer[HFTStorage::PacketSize];

  while (true) {

    ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);

    if (UNLIKELY(n & 7)) {
      packet_error();
      std::cout << "error\n";
      continue;
    }

    HFTStorage::Packet pkt;
    pkt.size = n;
    memcpy(pkt.data, buffer, n);

    if (UNLIKELY(!HFTStorage::PacketParseQueue.push(pkt))) {
        HFTStorage::dropped.fetch_add(1,std::memory_order_relaxed);
    }

    // process_packet(buffer, n);
    std::cout<<"count "<<count<<"\n";
  }
}


void run_packet_parser(){
  HFTStorage::Packet pkt;
  while (true) {
    if(HFTStorage::PacketParseQueue.pop(pkt)){
      process_packet(pkt, pkt.size);
    }
  }
}

} // namespace NetFeed
#endif