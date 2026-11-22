#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <type_traits>

#include "clashyuka/core/app_info.h"
#include "clashyuka/platform/platform_services.h"

TEST_CASE("platform services report the build platform") {
    const auto services = clashyuka::platform::CreatePlatformServices();
    REQUIRE(services != nullptr);
    CHECK_FALSE(services->Name().empty());

#if defined(_WIN32)
    CHECK(services->Name() == "Windows");
#elif defined(__APPLE__)
    CHECK(services->Name() == "macOS");
#elif defined(__linux__)
    CHECK(services->Name() == "Linux");
#endif
}

TEST_CASE("runtime info is populated") {
    const auto info = clashyuka::core::GetRuntimeInfo();
    CHECK_FALSE(info.platformName.empty());
    CHECK_FALSE(info.compiler.empty());
    CHECK(info.cppStandard.rfind("C++", 0) == 0);
}

TEST_CASE("nlohmann/json round trip") {
    const auto doc = nlohmann::json::parse(R"({"mode":"rule","mixed-port":7890})");
    CHECK(doc.at("mode").get<std::string>() == "rule");
    CHECK(doc.at("mixed-port").get<int>() == 7890);
    CHECK(doc.dump().find("rule") != std::string::npos);
}

TEST_CASE("yaml-cpp parses a minimal mihomo config") {
    const YAML::Node node = YAML::Load(
        "mixed-port: 7890\n"
        "mode: rule\n"
        "proxies: []\n");

    CHECK(node["mixed-port"].as<int>() == 7890);
    CHECK(node["mode"].as<std::string>() == "rule");
    CHECK(node["proxies"].IsSequence());
}

TEST_CASE("cpp-httplib exposes HTTP and WebSocket clients") {
    static_assert(std::is_class_v<httplib::Client>);
    static_assert(std::is_class_v<httplib::ws::WebSocketClient>);

    httplib::Client client("127.0.0.1", 9090);
    CHECK(client.host() == "127.0.0.1");
}
