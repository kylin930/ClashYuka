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
constexpr const char* kResponseMarker = "controller-response-marker";

bool HasValidBearer(const httplib::Request& request) {
    return request.get_header_value("Authorization") == std::string("Bearer ") + kSecret;
}

struct ApiFixture {
    MockServer server;
    std::atomic<bool> failVersion{false};
    std::atomic<bool> invalidJsonVersion{false};
    std::atomic<bool> slowVersion{false};
    std::atomic<bool> emptyProxies{false};
    std::atomic<bool> delayTimeout{false};
    std::atomic<bool> delayInvalid{false};
    std::mutex mutex;
    std::string lastSelectPath;
    std::string lastSelectBody;
    std::string lastPatchBody;
    std::string lastDelayTarget;
    std::string lastDelayUrl;
    std::string lastDelayTimeout;
    std::string lastDeleteTarget;

    bool Start() {
        auto& srv = server.server();

        srv.Get("/version", [this](const httplib::Request& request, httplib::Response& response) {
            if (!HasValidBearer(request)) {
                response.status = 401;
                response.set_content(kResponseMarker, "text/plain");
                return;
            }
            if (slowVersion.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            if (failVersion.load()) {
                response.status = 500;
                response.set_content(kResponseMarker, "text/plain");
                return;
            }
            if (invalidJsonVersion.load()) {
                response.set_content(std::string("\"") + kResponseMarker, "application/json");
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

        // Register the more specific delay route before the generic proxy route.
        srv.Get(R"(/proxies/(.+)/delay)",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!HasValidBearer(request)) {
                        response.status = 401;
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        const std::size_t query = request.target.find('?');
                        lastDelayTarget = request.target.substr(0, query);
                        lastDelayUrl = request.get_param_value("url");
                        lastDelayTimeout = request.get_param_value("timeout");
                    }
                    if (delayTimeout.load()) {
                        response.status = 504;
                        response.set_content("timeout", "text/plain");
                        return;
                    }
                    if (delayInvalid.load()) {
                        response.set_content(R"({"no-delay":true})", "application/json");
                        return;
                    }
                    response.set_content(R"({"delay":123})", "application/json");
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

        srv.Delete(R"(/proxies/(.+))",
                   [this](const httplib::Request& request, httplib::Response& response) {
                       if (!HasValidBearer(request)) {
                           response.status = 401;
                           return;
                       }
                       {
                           std::lock_guard<std::mutex> lock(mutex);
                           lastDeleteTarget = request.target;
                       }
                       response.status = 204;
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

TEST_CASE("Mihomo API closes only a single encoded connection with loopback bearer auth") {
    std::mutex mutex;
    std::string target;
    bool authenticated = false;
    std::atomic<int> calls{0};
    MockServer server;
    server.server().Delete(R"(/connections.*)", [&](const httplib::Request& request,
                                                     httplib::Response& response) {
        ++calls;
        std::lock_guard<std::mutex> lock(mutex);
        target = request.target;
        authenticated = HasValidBearer(request);
        if (!authenticated || request.path == "/connections/gone") {
            response.status = authenticated ? 404 : 401;
            response.set_content(kResponseMarker, "text/plain");
            return;
        }
        response.status = 204;
    });
    REQUIRE(server.Start());
    mihomo::MihomoApiConfig config;
    config.port = server.port();
    config.secret = kSecret;
    mihomo::MihomoApiClient client(config);
    REQUIRE(client.CloseConnection("id space/?#%+"));
    {
        std::lock_guard<std::mutex> lock(mutex);
        CHECK(authenticated);
        CHECK(target == "/connections/id%20space%2F%3F%23%25%2B");
    }
    const auto gone = client.CloseConnection("gone");
    REQUIRE_FALSE(gone);
    CHECK(gone.error().httpStatus == 404);
    CHECK(gone.error().message.find(kResponseMarker) == std::string::npos);
    CHECK_FALSE(client.CloseConnection(""));
    CHECK_FALSE(client.CloseConnection(".."));
    CHECK_FALSE(client.CloseConnection("prefix/.."));
    CHECK_FALSE(client.CloseConnection("/"));
    config.secret.clear();
    CHECK_FALSE(mihomo::MihomoApiClient(config).CloseConnection("id"));
    config.secret = kSecret;
    config.host = "192.0.2.1";
    const auto remote = mihomo::MihomoApiClient(config).CloseConnection("id");
    REQUIRE_FALSE(remote);
    CHECK(remote.error().kind == core::ErrorKind::InvalidConfig);
    CHECK(calls.load() == 2);
}

TEST_CASE("Mihomo rule provider update sends an authenticated PUT to the encoded name") {
    std::mutex mutex;
    std::string target;
    bool authenticated = false;
    MockServer server;
    server.server().Put(R"(/providers/rules/.*)", [&](const httplib::Request& request,
                                                      httplib::Response& response) {
        std::lock_guard<std::mutex> lock(mutex);
        target = request.target;
        authenticated = HasValidBearer(request);
        response.status = !authenticated ? 401 : request.path == "/providers/rules/missing" ? 404 : 204;
    });
    REQUIRE(server.Start());
    mihomo::MihomoApiConfig config;
    config.port = server.port();
    config.secret = kSecret;
    mihomo::MihomoApiClient client(config);
    REQUIRE(client.UpdateRuleProvider("\xE6\xB8\xAC test/one?#%"));
    {
        std::lock_guard<std::mutex> lock(mutex);
        CHECK(authenticated);
        CHECK(target == "/providers/rules/%E6%B8%AC%20test%2Fone%3F%23%25");
    }
    const auto missing = client.UpdateRuleProvider("missing");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().httpStatus == 404);
    CHECK_FALSE(client.UpdateRuleProvider(""));
    CHECK_FALSE(client.UpdateRuleProvider("name/.."));
}

TEST_CASE("Mihomo API client maps authentication and HTTP errors") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());

    mihomo::MihomoApiClient wrongSecret(fixture.Config("wrong-secret"));
    auto unauthorized = wrongSecret.GetVersion();
    REQUIRE_FALSE(unauthorized);
    CHECK(unauthorized.error().kind == core::ErrorKind::AuthFailed);
    CHECK(unauthorized.error().httpStatus == 401);
    CHECK(unauthorized.error().message == "authentication rejected by the controller");

    fixture.failVersion.store(true);
    mihomo::MihomoApiClient client(fixture.Config());
    auto failed = client.GetVersion();
    REQUIRE_FALSE(failed);
    CHECK(failed.error().kind == core::ErrorKind::HttpStatus);
    CHECK(failed.error().httpStatus == 500);
    CHECK(failed.error().message == "unexpected HTTP status 500");
}

TEST_CASE("Mihomo API client maps invalid JSON and missing fields") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    fixture.invalidJsonVersion.store(true);
    auto invalid = client.GetVersion();
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == core::ErrorKind::InvalidJson);
    CHECK(invalid.error().message == "response is not valid JSON");

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

TEST_CASE("Mihomo API client encodes delay tests and maps timeout responses") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    const std::string testUrl = "https://example.com/a?b=1&c=2";
    auto delay = client.TestProxyDelay("香港 #1 %", testUrl, 5000);
    REQUIRE(delay);
    CHECK(delay.value() == 123);

    {
        std::lock_guard<std::mutex> lock(fixture.mutex);
        CHECK(fixture.lastDelayTarget ==
              "/proxies/%E9%A6%99%E6%B8%AF%20%231%20%25/delay");
        CHECK(fixture.lastDelayUrl == testUrl);
        CHECK(fixture.lastDelayTimeout == "5000");
    }

    fixture.delayTimeout.store(true);
    auto timedOut = client.TestProxyDelay("node", "https://example.com", 5000);
    REQUIRE_FALSE(timedOut);
    CHECK(timedOut.error().kind == core::ErrorKind::Timeout);

    fixture.delayTimeout.store(false);
    fixture.delayInvalid.store(true);
    auto invalid = client.TestProxyDelay("node", "https://example.com", 5000);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == core::ErrorKind::MissingField);
}

TEST_CASE("Mihomo API client clears a fixed selection with an encoded DELETE") {
    ApiFixture fixture;
    REQUIRE(fixture.Start());
    mihomo::MihomoApiClient client(fixture.Config());

    auto cleared = client.ClearProxySelection("Auto 组/#1");
    REQUIRE(cleared);

    std::lock_guard<std::mutex> lock(fixture.mutex);
    CHECK(fixture.lastDeleteTarget == "/proxies/Auto%20%E7%BB%84%2F%231");
}
