#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "clashyuka/mihomo/event_client.h"
#include "mock/mock_server.h"

using namespace clashyuka;

namespace {

constexpr const char* kSecret = "ws-secret-91ad";

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

mihomo::MihomoEventConfig MakeConfig(int port, bool autoReconnect) {
    mihomo::MihomoEventConfig config;
    config.api.host = "127.0.0.1";
    config.api.port = port;
    config.api.secret = kSecret;
    config.api.connectionTimeoutMs = 500;
    config.autoReconnect = autoReconnect;
    config.reconnectDelayMs = 100;
    config.maxReconnectDelayMs = 400;
    return config;
}

}  // namespace

TEST_CASE("WebSocket traffic channel receives parsed messages and stops cleanly") {
    MockServer server;
    std::atomic<int> messages{0};
    std::atomic<double> lastUp{0};
    std::atomic<bool> authSeen{false};

    server.server().WebSocket(
        "/traffic", [&](const httplib::Request& request, httplib::ws::WebSocket& socket) {
            authSeen = request.get_header_value("Authorization") ==
                       std::string("Bearer ") + kSecret;
            for (int i = 0; i < 3; ++i) {
                socket.send(R"({"up":10,"down":20,"upTotal":100,"downTotal":200})");
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            socket.close();
        });

    REQUIRE(server.Start());

    mihomo::MihomoEventClient client(MakeConfig(server.port(), false));
    auto connected = client.ConnectTraffic([&](const mihomo::TrafficStatistics& traffic) {
        lastUp.store(traffic.up);
        ++messages;
    });
    REQUIRE(connected);

    CHECK(WaitUntil([&] { return messages.load() >= 3; }));
    CHECK(authSeen.load());
    CHECK(lastUp.load() == doctest::Approx(10));

    client.Disconnect(mihomo::EventChannel::Traffic);
    CHECK_FALSE(client.IsRunning(mihomo::EventChannel::Traffic));
}

TEST_CASE("WebSocket logs channel parses entries and passes the level parameter") {
    MockServer server;
    std::atomic<int> messages{0};
    std::atomic<bool> levelSeen{false};

    server.server().WebSocket(
        "/logs", [&](const httplib::Request& request, httplib::ws::WebSocket& socket) {
            levelSeen = request.target.find("level=warning") != std::string::npos;
            socket.send(R"({"type":"warning","payload":false})");
            socket.send(R"({"type":"warning","payload":")" + std::string(65536, 'x') + "\"}");
            socket.send(R"({"type":"warning","payload":"hello"})");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            socket.close();
        });

    REQUIRE(server.Start());

    mihomo::MihomoEventClient client(MakeConfig(server.port(), false));
    auto connected = client.ConnectLogs(
        [&](const mihomo::MihomoLogEntry& entry) {
            CHECK(entry.type == "warning");
            CHECK(entry.payload == "hello");
            ++messages;
        },
        "warning");
    REQUIRE(connected);

    CHECK(WaitUntil([&] { return messages.load() >= 1; }));
    CHECK(levelSeen.load());
    client.Disconnect();
    CHECK(messages.load() == 1);
}

TEST_CASE("WebSocket malformed messages are ignored without killing the channel") {
    MockServer server;
    std::atomic<int> validMessages{0};

    server.server().WebSocket(
        "/traffic", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            socket.send("this is not json");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            socket.send(R"({"up":5,"down":6})");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            socket.close();
        });

    REQUIRE(server.Start());

    mihomo::MihomoEventClient client(MakeConfig(server.port(), false));
    REQUIRE(client.ConnectTraffic([&](const mihomo::TrafficStatistics& traffic) {
        CHECK(traffic.up == doctest::Approx(5));
        ++validMessages;
    }));

    CHECK(WaitUntil([&] { return validMessages.load() >= 1; }));
    client.Disconnect();
}

TEST_CASE("WebSocket reconnects after the server closes the connection") {
    MockServer server;
    std::atomic<int> messages{0};
    std::atomic<int> connections{0};

    server.server().WebSocket(
        "/traffic", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            ++connections;
            socket.send(R"({"up":1,"down":1})");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            socket.close();
        });

    REQUIRE(server.Start());

    mihomo::MihomoEventClient client(MakeConfig(server.port(), true));
    REQUIRE(client.ConnectTraffic([&](const mihomo::TrafficStatistics&) { ++messages; }));

    CHECK(WaitUntil([&] { return messages.load() >= 3; }, 6000));
    CHECK(connections.load() >= 2);
    client.Disconnect();
}

TEST_CASE("WebSocket connections deliver an empty snapshot after reconnecting") {
    std::atomic<int> sessions{0};
    std::atomic<int> errors{0};
    std::atomic<bool> populated{false};
    std::atomic<bool> recovered{false};
    MockServer server;
    server.server().WebSocket(
        "/connections", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            if (++sessions == 1) {
                socket.send(R"({"connections":[{"id":"first","rule":"MATCH"}],"uploadTotal":12})");
            } else {
                socket.send(R"({"connections":null})");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            socket.close();
        });
    REQUIRE(server.Start());
    mihomo::MihomoEventClient client(MakeConfig(server.port(), true));
    REQUIRE(client.ConnectConnections([&](const mihomo::ConnectionsSnapshot& snapshot) {
        if (snapshot.connections.empty()) {
            recovered = errors.load() > 0;
        } else {
            populated = snapshot.connections.front().id == "first" &&
                        snapshot.connections.front().rule == "MATCH" && snapshot.uploadTotal == 12;
        }
    }, 1000, [&](const core::Error&) { ++errors; }));
    CHECK(WaitUntil([&] { return recovered.load(); }));
    CHECK(populated.load());
    client.Disconnect();
}

TEST_CASE("WebSocket disconnect returns promptly while a read is blocked") {
    MockServer server;
    server.server().WebSocket(
        "/traffic", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            socket.close();
        });

    REQUIRE(server.Start());

    mihomo::MihomoEventClient client(MakeConfig(server.port(), false));
    REQUIRE(client.ConnectTraffic([](const mihomo::TrafficStatistics&) {}));
    CHECK(WaitUntil([&] { return client.IsConnected(mihomo::EventChannel::Traffic); }));

    const auto start = std::chrono::steady_clock::now();
    client.Disconnect(mihomo::EventChannel::Traffic);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    CHECK(elapsed < 1000);
    CHECK_FALSE(client.IsRunning(mihomo::EventChannel::Traffic));
}

TEST_CASE("WebSocket client destructor disconnects without leaving workers") {
    MockServer server;
    server.server().WebSocket(
        "/memory", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            for (int i = 0; i < 10; ++i) {
                socket.send(R"({"inuse":1024,"oslimit":0})");
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            socket.close();
        });

    REQUIRE(server.Start());

    {
        mihomo::MihomoEventClient client(MakeConfig(server.port(), false));
        REQUIRE(client.ConnectMemory([](const mihomo::MihomoMemoryInfo&) {}));
        CHECK(WaitUntil([&] { return client.IsConnected(mihomo::EventChannel::Memory); }));
    }
    CHECK(true);
}
