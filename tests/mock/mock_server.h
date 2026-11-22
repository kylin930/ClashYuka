#pragma once

#include <httplib.h>

#include <thread>

// Small loopback HTTP/WebSocket server used by the Mihomo client tests.
class MockServer {
public:
    MockServer() = default;
    ~MockServer() { Stop(); }

    MockServer(const MockServer&) = delete;
    MockServer& operator=(const MockServer&) = delete;

    httplib::Server& server() { return server_; }

    bool Start() {
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) {
            return false;
        }
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
        return true;
    }

    void Stop() {
        if (thread_.joinable()) {
            server_.stop();
            thread_.join();
        }
    }

    int port() const { return port_; }

private:
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
};
