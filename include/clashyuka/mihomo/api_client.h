#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "clashyuka/core/result.h"
#include "clashyuka/mihomo/types.h"

namespace clashyuka::mihomo {

struct MihomoApiConfig {
    // External Controller endpoint. Defaults to loopback only.
    std::string host = "127.0.0.1";
    int port = 9090;
    std::string secret;
    int connectionTimeoutMs = 3000;
    int readTimeoutMs = 10000;

    // Optional probe used to distinguish "connection refused because the kernel
    // is not running" from other connection failures.
    std::function<bool()> kernelRunningProbe;
};

// Synchronous Mihomo REST client. Every call creates a short-lived connection,
// so one instance can safely be used from multiple worker threads.
// Never call these methods from the wxWidgets GUI thread.
class MihomoApiClient {
public:
    explicit MihomoApiClient(MihomoApiConfig config);
    ~MihomoApiClient();

    MihomoApiClient(const MihomoApiClient&) = delete;
    MihomoApiClient& operator=(const MihomoApiClient&) = delete;
    MihomoApiClient(MihomoApiClient&&) noexcept;
    MihomoApiClient& operator=(MihomoApiClient&&) noexcept;

    const MihomoApiConfig& Config() const;

    core::Result<MihomoVersion> GetVersion() const;
    core::Result<ProxyCollection> GetProxies() const;
    core::Result<ProxyInfo> GetProxy(std::string_view name) const;

    // PUT /proxies/{group} with {"name": target}
    core::Result<void> SelectProxy(std::string_view group, std::string_view target) const;

    // DELETE /proxies/{group} clears a fixed selection on URLTest/Fallback.
    core::Result<void> ClearProxySelection(std::string_view group) const;

    // GET /proxies/{name}/delay?url=...&timeout=...
    core::Result<int> TestProxyDelay(std::string_view name, std::string_view url,
                                     int timeoutMs) const;

    core::Result<RuntimeConfiguration> GetConfigs() const;
    core::Result<void> PatchConfigs(const nlohmann::json& patch) const;
    core::Result<RuleCollection> GetRules() const;
    core::Result<ConnectionsSnapshot> GetConnections() const;
    core::Result<ProxyProviderCollection> GetProxyProviders() const;
    core::Result<RuleProviderCollection> GetRuleProviders() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clashyuka::mihomo
