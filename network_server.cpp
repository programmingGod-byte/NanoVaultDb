#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <atomic>
#include <csignal>
#include "UDPReceiver.hpp"
#include "SQL_LEXER.hpp"
#include "SQL_PARSER.hpp"
#include "generator.hpp"
#include "logging.hpp"
#include "global.hpp"
#include "IndicatorHandler.hpp"
#include "Btrees_testing.hpp"
#include "web_socks_og.hpp"
#include "strategyHandler.hpp"
#include "utils/cpu_affinity.hpp"
#include "hft.hpp"

std::atomic<bool> serverRunning(true);

std::mutex clientMutex;
std::vector<std::thread> clientThreads;

int server_fd_global = -1;

void setup() {
  std::thread packet_receiver(NetFeed::run_receiver);
  std::thread packet_parser(NetFeed::run_packet_parser);
  std::thread strategy_parser(NetFeed::run_strategy_parser);
  std::thread web_socks(init_web_sockets);
  pin_thread_to_cpu(packet_receiver, 0);
  pin_thread_to_cpu(packet_parser, 1);
  pin_thread_to_cpu(strategy_parser,2);
  pin_thread_to_cpu(web_socks,3);

  packet_receiver.detach();
  packet_parser.detach();
  strategy_parser.detach();
  web_socks.detach();
}


void handleSignal(int) {
    serverRunning.store(false);
    shuttingDown.store(true);

    // This unblocks accept()
    if (server_fd_global != -1) {
        close(server_fd_global);
        server_fd_global = -1;
    }
}

std::string handleSQL(const std::string &sql) {
    try {
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        std::string r=parser.parse();
        return r;
    } catch (const std::exception &e) {
        return std::string("ERROR: ") + e.what() + "\n";
    }
}

bool sendAll(int fd, const std::string &data) {
    size_t totalSent = 0;
    const char *base = data.data();
    const size_t total = data.size();

    while (totalSent < total) {
        ssize_t sent = send(fd, base + totalSent, total - totalSent, MSG_NOSIGNAL);
        if (sent <= 0)
            return false;
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

static inline bool isExitOrQuit(std::string_view stmt) {
    if (stmt.size() > 32)
        return false;

    char trimmed[8];
    size_t n = 0;

    for (char c : stmt) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            if (n >= sizeof(trimmed))
                return false;
            trimmed[n++] = c;
        }
    }

    std::string_view t(trimmed, n);
    return t == "exit;" || t == "quit;";
}

void handleClient(int client_fd) {
    std::string clientBuffer;
    clientBuffer.reserve(8192);

    char buffer[4096];

    while (!shuttingDown.load()) {
        int bytesRead = read(client_fd, buffer, sizeof(buffer));

        if (bytesRead <= 0)
            break;

        clientBuffer.append(buffer, static_cast<size_t>(bytesRead));

        size_t start = 0;
        size_t pos;
        while ((pos = clientBuffer.find(';', start)) != std::string::npos) {
            std::string_view stmt(clientBuffer.data() + start, pos - start + 1);

            if (isExitOrQuit(stmt)) {
                close(client_fd);
                return;
            }

            std::string response = handleSQL(std::string(stmt));
            if (!sendAll(client_fd, response)) {
                close(client_fd);
                return;
            }

            start = pos + 1;
        }

        if (start > 0)
            clientBuffer.erase(0, start);
    }

    close(client_fd);
}


int main() {
    signal(SIGINT, handleSignal);
     initialDatabseLoad();
    HFT::InitalStorage::initialIndicatorLoad();
    HFT::InitalStorage::initialStrategyLoad();
    runVacuum();
    initializePrimaryIndexBtrees("abcd",true);
    test_b_trees();

    setup();


    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    server_fd_global = server_fd;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6970);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "NanoDB server running on port 6970...\n";

    while (serverRunning.load()) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!serverRunning.load())
                break;
            continue;
        }

        int one = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        std::lock_guard<std::mutex> lock(clientMutex);
        clientThreads.emplace_back(handleClient, client_fd);
    }

    std::cout << "Shutting down server...\n";

    shuttingDown.store(true);

    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (auto &t : clientThreads)
            if (t.joinable())
                t.join();
    }

    if (vacuumThread.joinable())
        vacuumThread.join();

    return 0;
}
