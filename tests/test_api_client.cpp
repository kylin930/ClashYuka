#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "clashyuka/mihomo/api_client.h"
#include "mock/mock_server.h"

using namespace clashyuka;
using nlohmann::json;

namespace {

constexpr const char* kSecret = "test-secret-2f7c1d";

bool HasValidBearer(const httplib::Request& request) {
    return request.get_header_value("Authorization") == std::string("Bearer ") + kSecret;
}

struct ApiFixture {
    MockServer server;
    std::atomic<bool> failVersion{false};
    std::atomic<bool> invalidJsonVersion{false};
    std::atomic<bool> slowVersion{false};
    std::atomic<bool> emptyProxies{false};
    std::mutex mutex;
    std::string lastSelectPath;
    std::string lastSelectBody;
    std::string lastPatchBody;

    bool Start() {
        auto& srv = server.server();

        srv.Get("/version", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                return;
            }
            if (slowVersion.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            if (failVersion.load()) {
                response.status = 500;
                response.set_content("internal error", "text/plain");
                return;
            }
            if (invalidJsonVersion.load()) {
                response.set_content("not-json", "application/json");
                return;
            }
            response.set_content(R"({"version":"1.19.20","meta":true})", "application/json");
        });

        srv.Get("/proxies", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                return;
            }
            if (emptyProxies.load()) {
                response.set_content(R"({"proxies":{}})", "application/json");
                return;
            }
            response.set_content(R"({
                "proxies": {
                    "DIRECT": {"name": "DIRECT", "type": "Direct", "udp": true},
                    "GLOBAL": {"name": "GLOBAL", "type": "Selector", "now": "DIRECT",
                               "all": ["DIRECT", "香港 01"]},
                    "香港 01": {"name": "香港 01", "type": "Trojan", "alive": true}
                }
            })",
                "application/json");
        });

        srv.Get(R"(/proxies/(.+))",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    response.set_content(R"({
                        "name": "香港 01",
                        "type": "Selector",
                        "now": "DIRECT",
                        "all": ["DIRECT"]
                    })",
                        "application/json");
                });

        srv.Put(R"(/proxies/(.+))",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        // req.path is already percent-decoded by cpp-httplib.
                        lastSelectPath = request.target;
                        lastSelectBody = request.body;
                    }
                    response.status = 204;
                });

        srv.Get("/configs", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                return;
            }
            response.set_content(
                R"({"port":7890,"socks-port":7891,"mixed-port":7892,"allow-lan":false,
                   "mode":"rule","log-level":"info"})",
                "application/json");
        });

        srv.Patch("/configs", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                lastPatchBody = request.body;
            }
            response.status = 204;
        });

        srv.Get("/rules", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                return;
            }
            response.set_content(
                R"({"rules":[{"index":0,"type":"MATCH","payload":"","proxy":"GLOBAL","size":-1}]})",
                "application/json");
        });

        srv.Get("/connections",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    response.set_content(
                        R"({"downloadTotal":10,"uploadTotal":20,"memory":30,
                            "connections":[{"id":"c1","metadata":{"network":"tcp"},
                                            "chains":["DIRECT"],"rule":"MATCH"}]})",
                        "application/json");
                });

        srv.Get("/providers/proxies",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    response.set_content(
                        R"({"providers":{"p":{"name":"p","type":"Proxy","proxies":[]}}})",
                        "application/json");
                });

        srv.Get("/providers/rules",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    response.set_content(
                        R"({"providers":{"r":{"name":"r","type":"Rule","ruleCount":3}}})",
                        "application/json");
                });

        return server.Start();
    }

    mihomo::MihomoApiConfig Config(std::string secret = kSecret, int readTimeoutMs = 2000) const {
        mihomo::MihomoApiConfig config;
        config.host = "127.0.0.1";
        config.port = server.port();
        config.secret = std::move(secret);
        config.connectionTimeoutMs = 1000;
        config.readTimeoutMs = readTimeoutMs;
        return config;
    }
};

}  // namespace

TEST_CASE("Mihomo API client reads version and proxies") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    auto version = client.GetVersion();
    REQUIRE(version);
    CHECK(version.value().version == "1.19.20");
    CHECK(version.value().meta);

    auto proxies = client.GetProxies();
    REQUIRE(proxies);
    REQUIRE(proxies.value().proxies.size() == 3);
    REQUIRE(proxies.value().Find("GLOBAL") != nullptr);
    CHECK(proxies.value().Find("GLOBAL")->is_group());

    auto single = client.GetProxy("香港 01");
    REQUIRE(single);
    CHECK(single.value().now == "DIRECT");

    auto config = client.GetConfigs();
    REQUIRE(config);
    CHECK(config.value().mixedPort == 7892);
    CHECK(config.value().mode == "rule");

    auto rules = client.GetRules();
    REQUIRE(rules);
    REQUIRE(rules.value().size() == 1);

    auto connections = client.GetConnections();
    REQUIRE(connections);
    REQUIRE(connections.value().connections.size() == 1);

    auto proxyProviders = client.GetProxyProviders();
    REQUIRE(proxyProviders);
    CHECK(proxyProviders.value().providers.size() == 1);

    auto ruleProviders = client.GetRuleProviders();
    REQUIRE(ruleProviders);
    CHECK(ruleProviders.value().providers[0].ruleCount == 3);
}

TEST_CASE("Mihomo API client encodes proxy group names and sends the selection body") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    auto selected = client.SelectProxy("香港 组/01", "节点 B");
    REQUIRE(selected);

    std::lock_guard<std::mutex> lock(fixture.mutex);
    CHECK(fixture.lastSelectPath ==
          "/proxies/%E9%A6%99%E6%B8%AF%20%E7%BB%84%2F01");
    const auto body = json::parse(fixture.lastSelectBody);
    CHECK(body.at("name") == "节点 B");
}

TEST_CASE("Mihomo API client maps authentication and HTTP errors") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());

    mihomo::MihomoApiClient wrongSecret(fixture.Config("wrong-secret"));
    auto unauthorized = wrongSecret.GetVersion();
    REQUIRE_FALSE(unauthorized);
    CHECK(unauthorized.error().kind == core::ErrorKind::AuthFailed);
    CHECK(unauthorized.error().httpStatus == 401);

    fixture.failVersion.store(true);
    mihomo::MihomoApiClient client(fixture.Config());
    auto failed = client.GetVersion();
    REQUIRE_FALSE(failed);
    CHECK(failed.error().kind == core::ErrorKind::HttpStatus);
    CHECK(failed.error().httpStatus == 500);
}

TEST_CASE("Mihomo API client maps invalid JSON and missing fields") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    fixture.invalidJsonVersion.store(true);
    auto invalid = client.GetVersion();
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == core::ErrorKind::InvalidJson);

    fixture.invalidJsonVersion.store(false);
    fixture.emptyProxies.store(true);
    auto empty = client.GetProxies();
    REQUIRE(empty);
    CHECK(empty.value().proxies.empty());
}

TEST_CASE("Mihomo API client distinguishes timeouts and a stopped kernel") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());

    fixture.slowVersion.store(true);
    mihomo::MihomoApiClient slowClient(fixture.Config(kSecret, 50));
    auto slow = slowClient.GetVersion();
    REQUIRE_FALSE(slow);
    CHECK(slow.error().kind == core::ErrorKind::Timeout);

    fixture.slowVersion.store(false);

    // Take a port that is not listening anymore.
    int closedPort = 0;
    {
        MockServer temporary;
        REQUIRE(temporary.Start());
        closedPort = temporary.port();
    }

    mihomo::MihomoApiConfig config;
    config.host = "127.0.0.1";
    config.port = closedPort;
    config.connectionTimeoutMs = 500;
    config.readTimeoutMs = 500;
    config.kernelRunningProbe = [] { return false; };

    mihomo::MihomoApiClient stoppedClient(config);
    auto stopped = stoppedClient.GetVersion();
    REQUIRE_FALSE(stopped);
    CHECK(stopped.error().kind == core::ErrorKind::KernelNotRunning);
}

TEST_CASE("Mihomo API client patches the running configuration") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    auto patched = client.PatchConfigs(json{{"mode", "global"}});
    REQUIRE(patched);

    std::lock_guard<std::mutex> lock(fixture.mutex);
    CHECK(json::parse(fixture.lastPatchBody).at("mode") == "global");
}

TEST_CASE("Mihomo API client rejects a wildcard controller host") {
    mihomo::MihomoApiConfig config;
    config.host = "0.0.0.0";
    mihomo::MihomoApiClient client(config);

    auto result = client.GetVersion();
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == core::ErrorKind::InvalidConfig);
}
