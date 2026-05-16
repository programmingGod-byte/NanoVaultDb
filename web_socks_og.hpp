#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <iomanip>
#include <sstream>

#include "global.hpp"
#include "utils/cpu_affinity.hpp"

namespace beast = boost::beast;         
namespace http = beast::http;           
namespace websocket = beast::websocket; 
namespace net = boost::asio;            
using tcp = boost::asio::ip::tcp;       

// Forward declaration
class WebSocketSession;

// Shared state to keep track of active sessions
class SharedState {
    std::mutex mutex_;
    std::vector<std::shared_ptr<WebSocketSession>> sessions_;

public:
    void join(std::shared_ptr<WebSocketSession> session);
    void leave(std::shared_ptr<WebSocketSession> session);
    void broadcast(const std::string& message);
};

// Handles a single WebSocket connection
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    std::shared_ptr<SharedState> state_;
    beast::flat_buffer buffer_;

public:
    WebSocketSession(tcp::socket&& socket, std::shared_ptr<SharedState> state)
        : ws_(std::move(socket)), state_(state) {}

    void run() {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept(beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) return;
        state_->join(shared_from_this());
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_, beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec == websocket::error::closed) {
            state_->leave(shared_from_this());
            return;
        }
        if (ec) {
            state_->leave(shared_from_this());
            return;
        }
        buffer_.consume(buffer_.size());
        do_read();
    }

    void send(const std::string& message) {
        auto ss = shared_from_this();
        net::dispatch(ws_.get_executor(), [ss, message]() {
            ss->ws_.async_write(net::buffer(message), [](beast::error_code, std::size_t) {});
        });
    }
};

// Implementations for SharedState methods after WebSocketSession is fully defined
inline void SharedState::join(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.push_back(std::move(session));
}

inline void SharedState::leave(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(sessions_.begin(), sessions_.end(), session);
    if (it != sessions_.end()) sessions_.erase(it);
}

inline void SharedState::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto const& session : sessions_) {
        session->send(message);
    }
}

// Accepts incoming connections and launches sessions
class Listener : public std::enable_shared_from_this<Listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<SharedState> state_;

public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint, std::shared_ptr<SharedState> state)
        : ioc_(ioc), acceptor_(ioc.get_executor()), state_(state) {
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
    }

    void run() { do_accept(); }

private:
    void do_accept() {
        acceptor_.async_accept(net::make_strand(ioc_), beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<WebSocketSession>(std::move(socket), state_)->run();
        }
        do_accept();
    }
};

inline void init_web_sockets(std::stop_token st, int cpu_id) {
    pin_thread_to_cpu(cpu_id);

    net::io_context ioc{1};
    auto state = std::make_shared<SharedState>();

    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(6969);
    std::make_shared<Listener>(ioc, tcp::endpoint{address, port}, state)->run();

    net::steady_timer timer(ioc, std::chrono::milliseconds(1));
    
    std::function<void(beast::error_code)> poll_queue;
    poll_queue = [&](beast::error_code ec) {
        if (ec || st.stop_requested()) return;

        web_socket_Packet pkt;
        while (web_socket_queue.pop(pkt)) {
            std::stringstream ss;
            ss << std::setw(4) << std::setfill('0') << pkt.symbol;
            ss << std::setw(4) << std::setfill('0') << pkt.strategyIndex;
            ss << "True";
            
            state->broadcast(ss.str());
        }

        timer.expires_after(std::chrono::milliseconds(1));
        timer.async_wait(poll_queue);
    };

    timer.async_wait(poll_queue);

    while (!st.stop_requested()) {
        ioc.poll();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    ioc.stop();
}