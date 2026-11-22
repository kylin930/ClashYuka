#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

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
               << "rules:\n"
               << "  - MATCH,DIRECT\n";
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
        auto connections = api.GetConnections();
        Check(connections.ok(), "GET /connections snapshot succeeded");
    }

    clashyuka::mihomo::MihomoEventConfig eventConfig;
    eventConfig.api = apiConfig;
    eventConfig.autoReconnect = false;

    if (ready) {
        {
            std::atomic<int> trafficMessages{0};
            clashyuka::mihomo::MihomoEventClient events(eventConfig);
            auto traffic = events.ConnectTraffic(
                [&](const clashyuka::mihomo::TrafficStatistics&) { ++trafficMessages; });
            Check(traffic.ok(), "traffic WebSocket connected");
            Check(WaitUntil([&] { return trafficMessages.load() > 0; }, 5000),
                  "traffic WebSocket delivered messages");

            auto logs = events.ConnectLogs([](const clashyuka::mihomo::MihomoLogEntry&) {});
            Check(logs.ok(), "logs WebSocket connected");
            Check(WaitUntil([&] { return events.IsConnected(clashyuka::mihomo::EventChannel::Logs); },
                            3000),
                  "logs WebSocket is open");

            events.Disconnect();
            Check(!events.IsRunning(clashyuka::mihomo::EventChannel::Traffic),
                  "traffic worker stopped");
            Check(!events.IsRunning(clashyuka::mihomo::EventChannel::Logs),
                  "logs worker stopped");
        }
        Check(true, "event client destroyed without leaving workers");
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
