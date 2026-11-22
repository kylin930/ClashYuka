#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

#include "clashyuka/mihomo/endpoint.h"
#include "clashyuka/mihomo/json.h"

using namespace clashyuka;
using nlohmann::json;

TEST_CASE("version JSON parsing") {
    SUBCASE("valid payload") {
        auto parsed = mihomo::ParseVersion(json::parse(R"({"version":"1.19.20","meta":true})"));
        REQUIRE(parsed);
        CHECK(parsed.value().version == "1.19.20");
        CHECK(parsed.value().meta);
    }

    SUBCASE("missing version field") {
        auto parsed = mihomo::ParseVersion(json::parse(R"({"meta":true})"));
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().kind == core::ErrorKind::MissingField);
    }

    SUBCASE("wrong field type") {
        auto parsed = mihomo::ParseVersion(json::parse(R"({"version":123})"));
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().kind == core::ErrorKind::TypeMismatch);
    }

    SUBCASE("null version") {
        auto parsed = mihomo::ParseVersion(json::parse(R"({"version":null})"));
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().kind == core::ErrorKind::MissingField);
    }

    SUBCASE("not an object") {
        auto parsed = mihomo::ParseVersion(json::parse("[]"));
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().kind == core::ErrorKind::TypeMismatch);
    }
}

TEST_CASE("proxy node JSON parsing") {
    const auto node = json::parse(R"({
        "name": "香港 01",
        "type": "Shadowsocks",
        "udp": true,
        "tfo": false,
        "alive": true,
        "history": [{"time": 1700000000, "delay": 120}, {"time": 1700000001, "delay": 0}],
        "future-field": {"nested": true}
    })");

    auto parsed = mihomo::ParseProxyInfo(node);
    REQUIRE(parsed);
    const auto& proxy = parsed.value();
    CHECK(proxy.name == "香港 01");
    CHECK(proxy.type == "Shadowsocks");
    CHECK(proxy.udp);
    CHECK(proxy.alive);
    CHECK(proxy.history.size() == 2);
    CHECK(proxy.history[0].delay == 120);
    CHECK_FALSE(proxy.is_group());
}

TEST_CASE("proxy group JSON parsing") {
    const auto group = json::parse(R"({
        "name": "GLOBAL",
        "type": "Selector",
        "now": "DIRECT",
        "all": ["DIRECT", "香港 01"],
        "fixed": "香港 01",
        "provider-name": "provider-a",
        "hidden": false,
        "history": []
    })");

    auto parsed = mihomo::ParseProxyInfo(group);
    REQUIRE(parsed);
    CHECK(parsed.value().is_group());
    CHECK(parsed.value().now == "DIRECT");
    CHECK(parsed.value().fixed == "香港 01");
    CHECK(parsed.value().providerName == "provider-a");
    REQUIRE(parsed.value().all.size() == 2);
    CHECK(parsed.value().all[1] == "香港 01");

    const auto balance = json::parse(R"({
        "name": "Balance", "type": "LoadBalance", "all": ["A", "B"]
    })");
    auto parsedBalance = mihomo::ParseProxyInfo(balance);
    REQUIRE(parsedBalance);
    CHECK(parsedBalance.value().is_group());
    CHECK(parsedBalance.value().now.empty());
    CHECK(parsedBalance.value().fixed.empty());
}

TEST_CASE("proxy collection tolerates malformed entries and unknown types") {
    const auto payload = json::parse(R"({
        "proxies": {
            "DIRECT": {"name": "DIRECT", "type": "Direct"},
            "REJECT": {"name": "REJECT", "type": "Reject"},
            "Broken": {"type": "Unknown"},
            "Null": null,
            "Custom": {"name": "Custom", "type": "Future-Protocol"}
        }
    })");

    auto parsed = mihomo::ParseProxies(payload);
    REQUIRE(parsed);
    CHECK(parsed.value().proxies.size() == 3);
    CHECK(parsed.value().Find("DIRECT") != nullptr);
    REQUIRE(parsed.value().Find("Custom") != nullptr);
    CHECK(parsed.value().Find("Custom")->type == "Future-Protocol");

    auto empty = mihomo::ParseProxies(json::parse(R"({"proxies":{}})"));
    REQUIRE(empty);
    CHECK(empty.value().proxies.empty());
}

TEST_CASE("rules JSON parsing") {
    const auto payload = json::parse(R"({
        "rules": [
            {"index": 0, "type": "DOMAIN", "payload": "example.com", "proxy": "DIRECT", "size": -1},
            {"type": "GEOIP", "payload": "CN", "proxy": "GLOBAL", "size": 1234}
        ]
    })");

    auto parsed = mihomo::ParseRules(payload);
    REQUIRE(parsed);
    REQUIRE(parsed.value().size() == 2);
    CHECK(parsed.value()[0].index == 0);
    CHECK(parsed.value()[1].index == 1);
    CHECK(parsed.value()[1].size == 1234);
}

TEST_CASE("runtime configuration parsing keeps unknown fields") {
    const auto payload = json::parse(R"({
        "port": 7890,
        "socks-port": 7891,
        "mixed-port": 7892,
        "allow-lan": true,
        "ipv6": false,
        "mode": "rule",
        "log-level": "info",
        "unknown-section": {"a": 1}
    })");

    auto parsed = mihomo::ParseConfigs(payload);
    REQUIRE(parsed);
    CHECK(parsed.value().port == 7890);
    CHECK(parsed.value().mixedPort == 7892);
    CHECK(parsed.value().mode == "rule");
    CHECK(parsed.value().logLevel == "info");
    CHECK(parsed.value().raw.contains("unknown-section"));
}

TEST_CASE("traffic, memory and log JSON parsing") {
    auto traffic = mihomo::ParseTraffic(
        json::parse(R"({"up":12,"down":34,"upTotal":100,"downTotal":200})"));
    REQUIRE(traffic);
    CHECK(traffic.value().up == doctest::Approx(12));
    CHECK(traffic.value().downTotal == doctest::Approx(200));

    auto emptyTraffic = mihomo::ParseTraffic(json::parse("{}"));
    REQUIRE(emptyTraffic);
    CHECK(emptyTraffic.value().up == doctest::Approx(0));

    auto memory = mihomo::ParseMemory(json::parse(R"({"inuse":1024,"oslimit":0})"));
    REQUIRE(memory);
    CHECK(memory.value().inuse == 1024);

    auto log = mihomo::ParseLog(json::parse(R"({"type":"info","payload":"hello"})"));
    REQUIRE(log);
    CHECK(log.value().type == "info");
    CHECK(log.value().payload == "hello");

    auto badLog = mihomo::ParseLog(json::parse(R"({"type":5})"));
    REQUIRE(badLog);
    CHECK(badLog.value().payload.empty());
}

TEST_CASE("connections JSON parsing handles nulls and empty arrays") {
    const auto payload = json::parse(R"({
        "downloadTotal": 100,
        "uploadTotal": 200,
        "memory": 300,
        "connections": [
            {
                "id": "conn-1",
                "upload": 1,
                "download": 2,
                "start": 1700000000,
                "chains": ["DIRECT"],
                "metadata": {
                    "network": "tcp",
                    "type": "HTTP",
                    "sourceIP": "127.0.0.1",
                    "destinationIP": "1.1.1.1",
                    "sourcePort": 1234,
                    "destinationPort": 80,
                    "host": "example.com",
                    "metadata-only": true
                },
                "rule": "MATCH",
                "rulePayload": ""
            },
            {"id": "conn-2", "metadata": null},
            null
        ]
    })");

    auto parsed = mihomo::ParseConnections(payload);
    REQUIRE(parsed);
    CHECK(parsed.value().connections.size() == 2);
    CHECK(parsed.value().connections[0].metadata.destinationPort == 80);
    CHECK(parsed.value().connections[0].chains.size() == 1);

    auto empty = mihomo::ParseConnections(json::parse(R"({"connections":[]})"));
    REQUIRE(empty);
    CHECK(empty.value().connections.empty());
}

TEST_CASE("provider JSON parsing") {
    auto proxies = mihomo::ParseProxyProviders(json::parse(R"({
        "providers": {
            "provider-a": {
                "name": "provider-a",
                "type": "Proxy",
                "vehicleType": "HTTP",
                "proxies": [{"name": "node", "type": "Trojan"}]
            },
            "empty": {"type": "Proxy", "proxies": []}
        }
    })"));
    REQUIRE(proxies);
    REQUIRE(proxies.value().providers.size() == 2);
    const auto provider = std::find_if(
        proxies.value().providers.begin(), proxies.value().providers.end(),
        [](const mihomo::ProxyProviderInfo& info) { return info.name == "provider-a"; });
    REQUIRE(provider != proxies.value().providers.end());
    CHECK(provider->proxies.size() == 1);

    auto rules = mihomo::ParseRuleProviders(json::parse(R"({
        "providers": {
            "geosite": {
                "name": "geosite",
                "type": "Rule",
                "behavior": "domain",
                "vehicleType": "HTTP",
                "ruleCount": 42
            }
        }
    })"));
    REQUIRE(rules);
    REQUIRE(rules.value().providers.size() == 1);
    CHECK(rules.value().providers[0].ruleCount == 42);
}

TEST_CASE("URL path segment encoding") {
    CHECK(mihomo::EncodePathSegment("DIRECT") == "DIRECT");
    CHECK(mihomo::EncodePathSegment("a-b_c.d~e") == "a-b_c.d~e");
    CHECK(mihomo::EncodePathSegment("Hong Kong") == "Hong%20Kong");
    CHECK(mihomo::EncodePathSegment("香港") == "%E9%A6%99%E6%B8%AF");
    CHECK(mihomo::EncodePathSegment("a/b?c#d") == "a%2Fb%3Fc%23d");
}

TEST_CASE("controller host and WebSocket URL validation") {
    CHECK(mihomo::IsWildcardAddress(""));
    CHECK(mihomo::IsWildcardAddress("0.0.0.0"));
    CHECK(mihomo::IsWildcardAddress("::"));
    CHECK_FALSE(mihomo::IsWildcardAddress("127.0.0.1"));
    CHECK_FALSE(mihomo::IsWildcardAddress("192.168.1.10"));

    CHECK(mihomo::IsLoopbackHost("127.0.0.1"));
    CHECK(mihomo::IsLoopbackHost("localhost"));
    CHECK(mihomo::IsLoopbackHost("::1"));
    CHECK_FALSE(mihomo::IsLoopbackHost("10.0.0.1"));

    CHECK(mihomo::BuildWebSocketUrl("127.0.0.1", 9090, "/traffic") ==
          "ws://127.0.0.1:9090/traffic");
    CHECK(mihomo::BuildWebSocketUrl("::1", 9090, "traffic") == "ws://[::1]:9090/traffic");
}
