#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../mock/mock_server.h"

#include "clashyuka/mihomo/api_client.h"
#include "clashyuka/mihomo/event_client.h"
#include "clashyuka/mihomo/process.h"

namespace {

int failures = 0;

void Check(bool condition, const std::string& description) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
    if (!condition) {
        ++failures;
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeoutMs) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 100) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return predicate();
}

std::string RandomHex(std::size_t length) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device device;
    std::mt19937 engine(device());
    std::uniform_int_distribution<int> distribution(0, 15);

    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(kHex[distribution(engine)]);
    }
    return result;
}

int RandomPort(std::mt19937& engine) {
    std::uniform_int_distribution<int> distribution(20000, 45000);
    return distribution(engine);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: clashyuka_mihomo_integration <path-to-mihomo>\n";
        return 2;
    }

    const std::filesystem::path mihomoPath = argv[1];
    if (!std::filesystem::exists(mihomoPath)) {
        std::cerr << "mihomo executable not found\n";
        return 2;
    }

    std::random_device device;
    std::mt19937 engine(device());
    const int apiPort = RandomPort(engine);
    int mixedPort = RandomPort(engine);
    while (mixedPort == apiPort) {
        mixedPort = RandomPort(engine);
    }
    const std::string secret = RandomHex(32);

    const std::filesystem::path home =
        std::filesystem::temp_directory_path() / ("clashyuka-p1-" + RandomHex(8));
    std::error_code error;
    std::filesystem::create_directories(home, error);
    if (error) {
        std::cerr << "cannot create temporary directory\n";
        return 2;
    }

    std::atomic<int> providerRequests{0};
    MockServer provider;
    provider.server().Get("/rules.yaml", [&](const httplib::Request&, httplib::Response& response) {
        ++providerRequests;
        response.set_content("payload:\n  - DOMAIN,fixture.example,DIRECT\n", "text/yaml");
    });
    if (!provider.Start()) {
        std::cerr << "cannot start loopback rule provider\n";
        return 2;
    }

    const std::filesystem::path configPath = home / "config.yaml";
    {
        std::ofstream config(configPath);
        config << "mixed-port: " << mixedPort << "\n"
               << "allow-lan: false\n"
               << "bind-address: 127.0.0.1\n"
               << "mode: rule\n"
               << "log-level: info\n"
               << "ipv6: false\n"
               << "external-controller: 127.0.0.1:" << apiPort << "\n"
               << "secret: \"" << secret << "\"\n"
               << "proxy-groups:\n"
               << "  - name: TEST\n"
               << "    type: select\n"
               << "    proxies:\n"
               << "      - DIRECT\n"
               << "      - REJECT\n"
               << "rule-providers:\n"
               << "  local-rules:\n"
               << "    type: http\n"
               << "    behavior: classical\n"
               << "    format: yaml\n"
               << "    url: http://127.0.0.1:" << provider.port() << "/rules.yaml\n"
               << "    path: ./providers/local-rules.yaml\n"
               << "    interval: 3600\n"
               << "rules:\n"
               << "  - RULE-SET,local-rules,DIRECT\n"
               << "  - IP-CIDR,127.0.0.1/32,DIRECT,no-resolve\n"
               << "  - MATCH,REJECT\n";
    }

    clashyuka::mihomo::MihomoProcess process;
    clashyuka::mihomo::MihomoProcessHandlers processHandlers;
    std::atomic<int> stdoutLines{0};
    processHandlers.onStdoutLine = [&](std::string_view) { ++stdoutLines; };
    processHandlers.onStderrLine = [](std::string_view) {};

    clashyuka::mihomo::MihomoLaunchOptions launch;
    launch.executablePath = mihomoPath.string();
    launch.arguments = {"-d", home.string(), "-f", configPath.string()};

    Check(process.Start(launch, processHandlers), "kernel process started");
    Check(process.GetPid().has_value(), "kernel pid available");

    clashyuka::mihomo::MihomoApiConfig apiConfig;
    apiConfig.host = "127.0.0.1";
    apiConfig.port = apiPort;
    apiConfig.secret = secret;
    apiConfig.connectionTimeoutMs = 1000;
    apiConfig.readTimeoutMs = 2000;
    apiConfig.kernelRunningProbe = [&] { return process.IsRunning(); };
    clashyuka::mihomo::MihomoApiClient api(apiConfig);

    const bool ready = WaitUntil(
        [&] {
            auto version = api.GetVersion();
            return version.ok();
        },
        20000);
    Check(ready, "GET /version became reachable");

    if (ready) {
        auto version = api.GetVersion();
        Check(version.ok() && !version.value().version.empty(), "version payload parsed");

        auto proxies = api.GetProxies();
        Check(proxies.ok(), "GET /proxies succeeded");
        if (proxies.ok()) {
            const auto* group = proxies.value().Find("TEST");
            Check(group != nullptr && group->is_group(), "TEST policy group present");

            auto selected = api.SelectProxy("TEST", "REJECT");
            Check(selected.ok(), "PUT /proxies/TEST selected REJECT");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            auto groupInfo = api.GetProxy("TEST");
            Check(groupInfo.ok() && groupInfo.value().now == "REJECT",
                  "TEST group now points at REJECT");

            auto restored = api.SelectProxy("TEST", "DIRECT");
            Check(restored.ok(), "PUT /proxies/TEST restored DIRECT");
        }

        auto configs = api.GetConfigs();
        Check(configs.ok() && configs.value().mixedPort == mixedPort,
              "GET /configs reports mixed-port");
        auto rules = api.GetRules();
        Check(rules.ok() && !rules.value().empty(), "GET /rules returned rules");
        if (rules.ok()) {
            Check(std::is_sorted(rules.value().begin(), rules.value().end(),
                      [](const clashyuka::mihomo::RuleInfo& left,
                         const clashyuka::mihomo::RuleInfo& right) {
                          return left.index < right.index;
                      }), "GET /rules preserves kernel rule order");
        }
        auto ruleProviders = api.GetRuleProviders();
        Check(ruleProviders.ok(), "GET /providers/rules succeeded");
        if (ruleProviders.ok()) {
            const auto fixtureProvider = std::find_if(ruleProviders.value().providers.begin(),
                ruleProviders.value().providers.end(), [](const clashyuka::mihomo::RuleProviderInfo& item) {
                    return item.name == "local-rules" && item.ruleCount == 1;
                });
            Check(fixtureProvider != ruleProviders.value().providers.end(),
                  "local HTTP rule provider exposed its fixture rule count");
        }
        Check(api.UpdateRuleProvider("local-rules").ok(),
              "PUT /providers/rules/local-rules refreshed the provider");
        Check(WaitUntil([&] { return providerRequests.load() >= 2; }, 5000),
              "provider refresh fetched the loopback fixture again");
        auto connections = api.GetConnections();
        Check(connections.ok(), "GET /connections snapshot succeeded");
    }

    clashyuka::mihomo::MihomoEventConfig eventConfig;
    eventConfig.api = apiConfig;
    eventConfig.autoReconnect = false;

    if (ready) {
        using namespace clashyuka::mihomo;
        std::promise<void> releaseResponse;
        auto responseGate = releaseResponse.get_future();
        std::atomic<bool> requestArrived{false};
        MockServer origin;
        origin.server().Get("/hold", [&](const httplib::Request&, httplib::Response& response) {
            requestArrived = true;
            // Keep the tracked connection alive until the close checks finish.
            responseGate.wait_for(std::chrono::seconds(25));
            response.set_content("loopback", "text/plain");
        });
        const bool originReady = origin.Start();
        Check(originReady, "HTTP origin listening on loopback");
        const std::string destination = "127.0.0.1:" + std::to_string(origin.port());

        std::atomic<int> trafficMessages{0};
        std::atomic<bool> routeLogged{false};
        std::mutex snapshotsMutex;
        ConnectionsSnapshot latest;
        int frames = 0;
        const auto matchesOrigin = [&](const ConnectionInfo& connection) {
            return !connection.id.empty() && connection.metadata.network == "tcp" &&
                   connection.metadata.destinationIP == "127.0.0.1" &&
                   connection.metadata.destinationPort == origin.port();
        };
        MihomoEventClient events(eventConfig);
        auto traffic = events.ConnectTraffic([&](const TrafficStatistics&) { ++trafficMessages; });
        Check(traffic.ok(), "traffic WebSocket connected");
        Check(WaitUntil([&] { return trafficMessages.load() > 0; }, 5000),
              "traffic WebSocket delivered messages");

        auto logs = events.ConnectLogs([&](const MihomoLogEntry& entry) {
            if (entry.type == "info" && entry.payload.find(destination) != std::string::npos &&
                entry.payload.find("DIRECT") != std::string::npos) {
                routeLogged = true;
            }
        });
        Check(logs.ok(), "logs WebSocket connected");
        const bool logsReady = WaitUntil([&] { return events.IsConnected(EventChannel::Logs); }, 3000);
        Check(logsReady, "logs WebSocket is open");

        auto connections = events.ConnectConnections([&](const ConnectionsSnapshot& snapshot) {
            std::lock_guard<std::mutex> lock(snapshotsMutex);
            latest = snapshot;
            ++frames;
        });
        Check(connections.ok(), "connections WebSocket connected");
        const bool emptyFrame = WaitUntil([&] {
            std::lock_guard<std::mutex> lock(snapshotsMutex);
            return frames > 0 && latest.connections.empty();
        }, 5000);
        Check(emptyFrame, "connections WebSocket delivered an empty snapshot before the request");

        if (originReady && logsReady && emptyFrame) {
            auto request = std::async(std::launch::async, [&] {
                httplib::Client client("127.0.0.1", origin.port());
                client.set_proxy("127.0.0.1", mixedPort);
                client.set_connection_timeout(2);
                client.set_read_timeout(30);
                return client.Get("/hold");
            });
            Check(WaitUntil([&] { return requestArrived.load(); }, 3000),
                  "proxied HTTP request reached the loopback origin");
            Check(WaitUntil([&] { return routeLogged.load(); }, 3000),
                  "logs WebSocket delivered the loopback DIRECT route log");

            std::string selectedId;
            Check(WaitUntil([&] {
                std::lock_guard<std::mutex> lock(snapshotsMutex);
                const auto found = std::find_if(latest.connections.begin(), latest.connections.end(),
                                                matchesOrigin);
                if (found == latest.connections.end()) {
                    return false;
                }
                selectedId = found->id;
                return true;
            }, 5000), "connections WebSocket delivered the held-open TCP connection ID");

            if (!selectedId.empty()) {
                const auto closed = api.CloseConnection(selectedId);
                Check(closed.ok(), "DELETE /connections/{id} closed the selected ID");
                int framesAfterClose;
                {
                    std::lock_guard<std::mutex> lock(snapshotsMutex);
                    framesAfterClose = frames;
                }
                Check(WaitUntil([&] {
                    std::lock_guard<std::mutex> lock(snapshotsMutex);
                    return frames > framesAfterClose &&
                           std::none_of(latest.connections.begin(), latest.connections.end(),
                                        [&](const ConnectionInfo& entry) { return entry.id == selectedId; });
                }, 5000), "a subsequent connections frame omitted the closed ID");
            }
            releaseResponse.set_value();
            request.wait();
        }

        events.Disconnect();
        Check(!events.IsRunning(EventChannel::Traffic), "traffic worker stopped");
        Check(!events.IsRunning(EventChannel::Logs), "logs worker stopped");
        Check(!events.IsRunning(EventChannel::Connections), "connections worker stopped");
    }

    process.Stop();
    Check(!process.IsRunning(), "kernel stopped");
    Check(!process.GetPid().has_value(), "kernel pid cleared (no orphan)");

    std::filesystem::remove_all(home, error);

    if (failures == 0) {
        std::cout << "INTEGRATION SUCCESS\n";
        return 0;
    }

    std::cout << "INTEGRATION FAILED: " << failures << " check(s)\n";
    return 1;
}
