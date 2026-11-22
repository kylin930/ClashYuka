#pragma once

#include <nlohmann/json.hpp>

#include "clashyuka/core/result.h"
#include "clashyuka/mihomo/types.h"

namespace clashyuka::mihomo {

core::Result<MihomoVersion> ParseVersion(const nlohmann::json& json);
core::Result<ProxyInfo> ParseProxyInfo(const nlohmann::json& json);
core::Result<ProxyCollection> ParseProxies(const nlohmann::json& json);
core::Result<RuleCollection> ParseRules(const nlohmann::json& json);
core::Result<RuntimeConfiguration> ParseConfigs(const nlohmann::json& json);
core::Result<TrafficStatistics> ParseTraffic(const nlohmann::json& json);
core::Result<MihomoMemoryInfo> ParseMemory(const nlohmann::json& json);
core::Result<ConnectionsSnapshot> ParseConnections(const nlohmann::json& json);
core::Result<MihomoLogEntry> ParseLog(const nlohmann::json& json);
core::Result<ProxyProviderCollection> ParseProxyProviders(const nlohmann::json& json);
core::Result<RuleProviderCollection> ParseRuleProviders(const nlohmann::json& json);

}  // namespace clashyuka::mihomo
