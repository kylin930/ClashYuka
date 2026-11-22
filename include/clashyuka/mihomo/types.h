#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace clashyuka::mihomo {

struct MihomoVersion {
    std::string version;
    bool meta = false;
};

struct ProxyHistory {
    std::int64_t time = 0;
    int delay = 0;
};

// A proxy node or a policy group. Policy groups additionally carry `now`/`all`.
struct ProxyInfo {
    std::string name;
    std::string type;
    bool udp = false;
    bool uot = false;
    bool xudp = false;
    bool tfo = false;
    bool mptcp = false;
    bool smux = false;
    bool alive = false;
    std::vector<ProxyHistory> history;
    std::string now;
    std::vector<std::string> all;
    std::string fixed;
    std::string providerName;
    std::string testUrl;
    std::string icon;
    bool hidden = false;

    bool is_group() const { return !all.empty() || !now.empty(); }
};

struct ProxyCollection {
    std::vector<ProxyInfo> proxies;

    const ProxyInfo* Find(std::string_view name) const;
};

struct RuleInfo {
    int index = 0;
    std::string type;
    std::string payload;
    std::string proxy;
    int size = -1;
};

using RuleCollection = std::vector<RuleInfo>;

struct RuntimeConfiguration {
    int port = 0;
    int socksPort = 0;
    int mixedPort = 0;
    bool allowLan = false;
    bool ipv6 = false;
    std::string mode;
    std::string logLevel;
    nlohmann::json raw;
};

// Rates are bytes per second, totals are bytes.
struct TrafficStatistics {
    double up = 0.0;
    double down = 0.0;
    double upTotal = 0.0;
    double downTotal = 0.0;
};

struct MihomoMemoryInfo {
    std::uint64_t inuse = 0;
    std::uint64_t oslimit = 0;
};

struct ConnectionMetadata {
    std::string network;
    std::string type;
    std::string sourceIP;
    std::string destinationIP;
    std::string host;
    std::string dnsMode;
    std::string processPath;
    int sourcePort = 0;
    int destinationPort = 0;
};

struct ConnectionInfo {
    std::string id;
    ConnectionMetadata metadata;
    std::uint64_t upload = 0;
    std::uint64_t download = 0;
    std::uint64_t start = 0;
    std::vector<std::string> chains;
    std::vector<std::string> providerChains;
    std::string rule;
    std::string rulePayload;
};

struct ConnectionsSnapshot {
    std::uint64_t downloadTotal = 0;
    std::uint64_t uploadTotal = 0;
    std::uint64_t memory = 0;
    std::vector<ConnectionInfo> connections;
};

struct MihomoLogEntry {
    std::string type;
    std::string payload;
};

struct ProxyProviderInfo {
    std::string name;
    std::string type;
    std::string vehicleType;
    std::uint64_t updatedAt = 0;
    std::vector<ProxyInfo> proxies;
};

struct ProxyProviderCollection {
    std::vector<ProxyProviderInfo> providers;
};

struct RuleProviderInfo {
    std::string name;
    std::string type;
    std::string behavior;
    std::string vehicleType;
    std::uint64_t updatedAt = 0;
    int ruleCount = 0;
};

struct RuleProviderCollection {
    std::vector<RuleProviderInfo> providers;
};

}  // namespace clashyuka::mihomo
