#include "clashyuka/mihomo/json.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

namespace clashyuka::mihomo {
namespace {

using core::Error;
using core::ErrorKind;
using core::MakeError;
using Json = nlohmann::json;

const Json* Member(const Json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return nullptr;
    }
    return &(*it);
}

std::string StringOr(const Json& object, const char* key, std::string fallback = {}) {
    const Json* value = Member(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::move(fallback);
}

bool BoolOr(const Json& object, const char* key, bool fallback = false) {
    const Json* value = Member(object, key);
    return value != nullptr && value->is_boolean() ? value->get<bool>() : fallback;
}

int IntOr(const Json& object, const char* key, int fallback = 0) {
    const Json* value = Member(object, key);
    if (value == nullptr || !value->is_number()) {
        return fallback;
    }
    return static_cast<int>(value->get<double>());
}

int PortOrZero(const Json& metadata, const char* key) {
    const auto* value = Member(metadata, key);
    if (value == nullptr) {
        return 0;
    }
    if (value->is_number_integer() && *value >= 0 && *value <= 65535) {
        return value->get<int>();
    }
    if (value->is_string()) {
        const auto& text = value->get_ref<const std::string&>();
        int port = 0;
        const auto end = text.data() + text.size();
        const auto result = std::from_chars(text.data(), end, port);
        if (result.ec == std::errc{} && result.ptr == end && port >= 0 && port <= 65535) {
            return port;
        }
    }
    return 0;
}

std::int64_t Int64Or(const Json& object, const char* key, std::int64_t fallback = 0) {
    const Json* value = Member(object, key);
    if (value == nullptr || !value->is_number()) {
        return fallback;
    }
    return static_cast<std::int64_t>(value->get<double>());
}

std::uint64_t UInt64Or(const Json& object, const char* key, std::uint64_t fallback = 0) {
    const Json* value = Member(object, key);
    if (value == nullptr || !value->is_number()) {
        return fallback;
    }
    if (value->is_number_unsigned()) {
        return value->get<std::uint64_t>();
    }
    if (value->is_number_integer()) {
        const auto number = value->get<std::int64_t>();
        return number >= 0 ? static_cast<std::uint64_t>(number) : fallback;
    }
    const double number = value->get<double>();
    return number >= 0 && number < 0x1p64 ? static_cast<std::uint64_t>(number) : fallback;
}

double DoubleOr(const Json& object, const char* key, double fallback = 0.0) {
    const Json* value = Member(object, key);
    return value != nullptr && value->is_number() ? value->get<double>() : fallback;
}

bool RequireString(const Json& object, const char* key, std::string& out, Error& error) {
    const Json* value = Member(object, key);
    if (value == nullptr) {
        error = MakeError(ErrorKind::MissingField, std::string("missing field '") + key + "'");
        return false;
    }
    if (!value->is_string()) {
        error = MakeError(ErrorKind::TypeMismatch, std::string("field '") + key + "' is not a string");
        return false;
    }
    out = value->get<std::string>();
    return true;
}

std::vector<std::string> StringArray(const Json& object, const char* key) {
    std::vector<std::string> result;
    const Json* value = Member(object, key);
    if (value == nullptr || !value->is_array()) {
        return result;
    }
    result.reserve(value->size());
    for (const auto& item : *value) {
        if (item.is_string()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

bool RequireObject(const Json& json, const char* what, Error& error) {
    if (!json.is_object()) {
        error = MakeError(ErrorKind::TypeMismatch, std::string(what) + " is not a JSON object");
        return false;
    }
    return true;
}

}  // namespace

core::Result<MihomoVersion> ParseVersion(const Json& json) {
    Error error;
    if (!RequireObject(json, "version payload", error)) {
        return error;
    }

    MihomoVersion version;
    if (!RequireString(json, "version", version.version, error)) {
        return error;
    }
    version.meta = BoolOr(json, "meta");
    return version;
}

core::Result<ProxyInfo> ParseProxyInfo(const Json& json) {
    Error error;
    if (!RequireObject(json, "proxy", error)) {
        return error;
    }

    ProxyInfo proxy;
    if (!RequireString(json, "name", proxy.name, error) ||
        !RequireString(json, "type", proxy.type, error)) {
        return error;
    }

    proxy.udp = BoolOr(json, "udp");
    proxy.uot = BoolOr(json, "uot");
    proxy.xudp = BoolOr(json, "xudp");
    proxy.tfo = BoolOr(json, "tfo");
    proxy.mptcp = BoolOr(json, "mptcp");
    proxy.smux = BoolOr(json, "smux");
    proxy.alive = BoolOr(json, "alive");
    proxy.now = StringOr(json, "now");
    proxy.all = StringArray(json, "all");
    proxy.fixed = StringOr(json, "fixed");
    proxy.providerName = StringOr(json, "provider-name");
    proxy.testUrl = StringOr(json, "testUrl");
    proxy.icon = StringOr(json, "icon");
    proxy.hidden = BoolOr(json, "hidden");

    if (const Json* history = Member(json, "history"); history != nullptr && history->is_array()) {
        proxy.history.reserve(history->size());
        for (const auto& item : *history) {
            if (!item.is_object()) {
                continue;
            }
            ProxyHistory entry;
            entry.time = Int64Or(item, "time");
            entry.delay = IntOr(item, "delay");
            proxy.history.push_back(entry);
        }
    }

    return proxy;
}

core::Result<ProxyCollection> ParseProxies(const Json& json) {
    Error error;
    if (!RequireObject(json, "proxies payload", error)) {
        return error;
    }

    const Json* proxies = Member(json, "proxies");
    if (proxies == nullptr) {
        return MakeError(ErrorKind::MissingField, "missing field 'proxies'");
    }
    if (!proxies->is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "field 'proxies' is not an object");
    }

    ProxyCollection collection;
    collection.proxies.reserve(proxies->size());
    for (const auto& item : proxies->items()) {
        if (!item.value().is_object()) {
            continue;
        }
        auto parsed = ParseProxyInfo(item.value());
        if (!parsed) {
            // A single malformed entry must not hide the rest of the list.
            continue;
        }
        collection.proxies.push_back(parsed.Take());
    }
    return collection;
}

core::Result<RuleCollection> ParseRules(const Json& json) {
    Error error;
    if (!RequireObject(json, "rules payload", error)) {
        return error;
    }

    const Json* rules = Member(json, "rules");
    if (rules == nullptr) {
        return MakeError(ErrorKind::MissingField, "missing field 'rules'");
    }
    if (!rules->is_array()) {
        return MakeError(ErrorKind::TypeMismatch, "field 'rules' is not an array");
    }

    RuleCollection collection;
    collection.reserve(rules->size());
    int position = 0;
    for (const auto& item : *rules) {
        if (!item.is_object()) {
            ++position;
            continue;
        }
        RuleInfo rule;
        rule.index = IntOr(item, "index", position);
        rule.type = StringOr(item, "type");
        rule.payload = StringOr(item, "payload");
        rule.proxy = StringOr(item, "proxy");
        rule.size = IntOr(item, "size", -1);
        collection.push_back(std::move(rule));
        ++position;
    }
    return collection;
}

core::Result<RuntimeConfiguration> ParseConfigs(const Json& json) {
    Error error;
    if (!RequireObject(json, "configs payload", error)) {
        return error;
    }

    RuntimeConfiguration config;
    config.port = IntOr(json, "port");
    config.socksPort = IntOr(json, "socks-port");
    config.mixedPort = IntOr(json, "mixed-port");
    config.allowLan = BoolOr(json, "allow-lan");
    config.ipv6 = BoolOr(json, "ipv6");
    config.mode = StringOr(json, "mode");
    config.logLevel = StringOr(json, "log-level");
    config.raw = json;
    return config;
}

core::Result<TrafficStatistics> ParseTraffic(const Json& json) {
    if (!json.is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "traffic payload is not a JSON object");
    }

    TrafficStatistics traffic;
    traffic.up = DoubleOr(json, "up");
    traffic.down = DoubleOr(json, "down");
    traffic.upTotal = DoubleOr(json, "upTotal");
    traffic.downTotal = DoubleOr(json, "downTotal");
    return traffic;
}

core::Result<MihomoMemoryInfo> ParseMemory(const Json& json) {
    if (!json.is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "memory payload is not a JSON object");
    }

    MihomoMemoryInfo memory;
    memory.inuse = UInt64Or(json, "inuse");
    memory.oslimit = UInt64Or(json, "oslimit");
    return memory;
}

core::Result<ConnectionsSnapshot> ParseConnections(const Json& json) {
    Error error;
    if (!RequireObject(json, "connections payload", error)) {
        return error;
    }

    ConnectionsSnapshot snapshot;
    snapshot.downloadTotal = UInt64Or(json, "downloadTotal");
    snapshot.uploadTotal = UInt64Or(json, "uploadTotal");
    snapshot.memory = UInt64Or(json, "memory");

    const auto connections = json.find("connections");
    if (connections == json.end()) {
        return MakeError(ErrorKind::MissingField, "missing field 'connections'");
    }
    // A nil Mihomo connection slice can be serialized as null.
    if (connections->is_null()) {
        return snapshot;
    }
    if (!connections->is_array()) {
        return MakeError(ErrorKind::TypeMismatch, "field 'connections' is not an array");
    }

    snapshot.connections.reserve(connections->size());
    for (const auto& item : *connections) {
        if (!item.is_object()) {
            continue;
        }

        ConnectionInfo connection;
        connection.id = StringOr(item, "id");
        connection.upload = UInt64Or(item, "upload");
        connection.download = UInt64Or(item, "download");
        connection.start = UInt64Or(item, "start");
        connection.chains = StringArray(item, "chains");
        connection.providerChains = StringArray(item, "providerChains");
        connection.rule = StringOr(item, "rule");
        connection.rulePayload = StringOr(item, "rulePayload");

        if (const Json* metadata = Member(item, "metadata");
            metadata != nullptr && metadata->is_object()) {
            connection.metadata.network = StringOr(*metadata, "network");
            connection.metadata.type = StringOr(*metadata, "type");
            connection.metadata.sourceIP = StringOr(*metadata, "sourceIP");
            connection.metadata.destinationIP = StringOr(*metadata, "destinationIP");
            connection.metadata.host = StringOr(*metadata, "host");
            connection.metadata.dnsMode = StringOr(*metadata, "dnsMode");
            connection.metadata.processPath = StringOr(*metadata, "processPath");
            connection.metadata.sourcePort = PortOrZero(*metadata, "sourcePort");
            connection.metadata.destinationPort = PortOrZero(*metadata, "destinationPort");
        }

        snapshot.connections.push_back(std::move(connection));
    }
    return snapshot;
}

core::Result<MihomoLogEntry> ParseLog(const Json& json) {
    if (!json.is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "log payload is not a JSON object");
    }

    const Json* type = Member(json, "type");
    const Json* payload = Member(json, "payload");
    if (type == nullptr || payload == nullptr) {
        return MakeError(ErrorKind::MissingField, "log message requires type and payload");
    }
    if (!type->is_string() || !payload->is_string() || type->get_ref<const std::string&>().empty()) {
        return MakeError(ErrorKind::TypeMismatch, "invalid log message type or payload");
    }
    return MihomoLogEntry{type->get<std::string>(), payload->get<std::string>()};
}

core::Result<ProxyProviderCollection> ParseProxyProviders(const Json& json) {
    Error error;
    if (!RequireObject(json, "proxy providers payload", error)) {
        return error;
    }

    const Json* providers = Member(json, "providers");
    if (providers == nullptr) {
        return MakeError(ErrorKind::MissingField, "missing field 'providers'");
    }
    if (!providers->is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "field 'providers' is not an object");
    }

    ProxyProviderCollection collection;
    collection.providers.reserve(providers->size());
    for (const auto& item : providers->items()) {
        if (!item.value().is_object()) {
            continue;
        }

        ProxyProviderInfo provider;
        provider.name = StringOr(item.value(), "name", item.key());
        provider.type = StringOr(item.value(), "type");
        provider.vehicleType = StringOr(item.value(), "vehicleType");
        provider.updatedAt = UInt64Or(item.value(), "updatedAt");

        if (const Json* proxies = Member(item.value(), "proxies");
            proxies != nullptr && proxies->is_array()) {
            provider.proxies.reserve(proxies->size());
            for (const auto& proxy : *proxies) {
                if (!proxy.is_object()) {
                    continue;
                }
                auto parsed = ParseProxyInfo(proxy);
                if (parsed) {
                    provider.proxies.push_back(parsed.Take());
                }
            }
        }
        collection.providers.push_back(std::move(provider));
    }
    return collection;
}

core::Result<RuleProviderCollection> ParseRuleProviders(const Json& json) {
    Error error;
    if (!RequireObject(json, "rule providers payload", error)) {
        return error;
    }

    const Json* providers = Member(json, "providers");
    if (providers == nullptr) {
        return MakeError(ErrorKind::MissingField, "missing field 'providers'");
    }
    if (!providers->is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "field 'providers' is not an object");
    }

    RuleProviderCollection collection;
    collection.providers.reserve(providers->size());
    for (const auto& item : providers->items()) {
        if (!item.value().is_object()) {
            continue;
        }

        RuleProviderInfo provider;
        // The object key is the name accepted by the provider update endpoint.
        provider.name = item.key();
        provider.type = StringOr(item.value(), "type");
        provider.behavior = StringOr(item.value(), "behavior");
        provider.vehicleType = StringOr(item.value(), "vehicleType");
        provider.updatedAt = StringOr(item.value(), "updatedAt");
        if (const auto timestamp = UInt64Or(item.value(), "updatedAt"); timestamp != 0) {
            provider.updatedAt = std::to_string(timestamp);
        }
        provider.ruleCount = IntOr(item.value(), "ruleCount");
        collection.providers.push_back(std::move(provider));
    }
    return collection;
}

}  // namespace clashyuka::mihomo
