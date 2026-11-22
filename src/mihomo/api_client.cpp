#include "clashyuka/mihomo/api_client.h"

#include <utility>

#include <httplib.h>

#include "clashyuka/mihomo/endpoint.h"
#include "clashyuka/mihomo/json.h"

namespace clashyuka::mihomo {
namespace {

using core::Error;
using core::ErrorKind;
using core::MakeError;

std::string Truncate(std::string value, std::size_t limit = 512) {
    if (value.size() > limit) {
        value.resize(limit);
        value += "...";
    }
    return value;
}

core::Result<std::string> Request(const MihomoApiConfig& config, const char* method,
                                  const std::string& path, const std::string& body = {},
                                  const char* content_type = nullptr) {
    if (config.host.empty() || IsWildcardAddress(config.host)) {
        return MakeError(ErrorKind::InvalidConfig,
                         "invalid controller host '" + config.host + "'");
    }
    if (config.port <= 0 || config.port > 65535) {
        return MakeError(ErrorKind::InvalidConfig, "invalid controller port");
    }

    httplib::Client client(config.host, config.port);
    client.set_connection_timeout(static_cast<time_t>(config.connectionTimeoutMs / 1000),
                                  static_cast<time_t>(config.connectionTimeoutMs % 1000) * 1000);
    client.set_read_timeout(static_cast<time_t>(config.readTimeoutMs / 1000),
                            static_cast<time_t>(config.readTimeoutMs % 1000) * 1000);
    client.set_write_timeout(static_cast<time_t>(config.readTimeoutMs / 1000),
                             static_cast<time_t>(config.readTimeoutMs % 1000) * 1000);

    httplib::Headers headers;
    if (!config.secret.empty()) {
        headers.emplace("Authorization", "Bearer " + config.secret);
    }
    if (content_type != nullptr) {
        headers.emplace("Content-Type", content_type);
    }
    if (!headers.empty()) {
        client.set_default_headers(headers);
    }

    httplib::Result result;
    const std::string verb(method);
    if (verb == "GET") {
        result = client.Get(path);
    } else if (verb == "PUT") {
        result = client.Put(path, body, content_type == nullptr ? "application/json" : content_type);
    } else if (verb == "PATCH") {
        result = client.Patch(path, body, content_type == nullptr ? "application/json" : content_type);
    } else {
        return MakeError(ErrorKind::Internal, "unsupported HTTP method " + verb);
    }

    if (!result) {
        switch (result.error()) {
            case httplib::Error::Read:
            case httplib::Error::Write:
                // A read/write timeout surfaces as Read/Write in cpp-httplib.
                return MakeError(ErrorKind::Timeout, "request timed out");
            case httplib::Error::ConnectionTimeout:
            case httplib::Error::Timeout:
                if (config.kernelRunningProbe && !config.kernelRunningProbe()) {
                    return MakeError(ErrorKind::KernelNotRunning,
                                     "cannot reach the External Controller; kernel is not running");
                }
                return MakeError(ErrorKind::Timeout, "request timed out");
            case httplib::Error::Connection:
            case httplib::Error::ConnectionClosed:
            case httplib::Error::ProxyConnection: {
                if (config.kernelRunningProbe && !config.kernelRunningProbe()) {
                    return MakeError(ErrorKind::KernelNotRunning,
                                     "cannot reach the External Controller; kernel is not running");
                }
                return MakeError(ErrorKind::ConnectionFailed,
                                 "cannot reach the External Controller at " + config.host + ":" +
                                     std::to_string(config.port));
            }
            default:
                return MakeError(ErrorKind::ConnectionFailed,
                                 "HTTP request failed: " +
                                     std::string(httplib::to_string(result.error())));
        }
    }

    const int status = result->status;
    if (status == 401 || status == 403) {
        return MakeError(ErrorKind::AuthFailed, "authentication rejected by the controller",
                         status);
    }
    if (status < 200 || status >= 300) {
        return MakeError(ErrorKind::HttpStatus,
                         "unexpected HTTP status " + std::to_string(status) + ": " +
                             Truncate(result->body),
                         status);
    }

    return result->body;
}

core::Result<nlohmann::json> RequestJson(const MihomoApiConfig& config, const char* method,
                                         const std::string& path,
                                         const std::string& body = {}) {
    auto response = Request(config, method, path, body);
    if (!response) {
        return response.error();
    }

    nlohmann::json json = nlohmann::json::parse(response.value(), nullptr, false);
    if (json.is_discarded()) {
        return MakeError(ErrorKind::InvalidJson, "response is not valid JSON");
    }
    return json;
}

}  // namespace

struct MihomoApiClient::Impl {
    explicit Impl(MihomoApiConfig value) : config(std::move(value)) {}
    MihomoApiConfig config;
};

MihomoApiClient::MihomoApiClient(MihomoApiConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MihomoApiClient::~MihomoApiClient() = default;
MihomoApiClient::MihomoApiClient(MihomoApiClient&&) noexcept = default;
MihomoApiClient& MihomoApiClient::operator=(MihomoApiClient&&) noexcept = default;

const MihomoApiConfig& MihomoApiClient::Config() const {
    return impl_->config;
}

core::Result<MihomoVersion> MihomoApiClient::GetVersion() const {
    auto json = RequestJson(impl_->config, "GET", "/version");
    if (!json) {
        return json.error();
    }
    return ParseVersion(json.value());
}

core::Result<ProxyCollection> MihomoApiClient::GetProxies() const {
    auto json = RequestJson(impl_->config, "GET", "/proxies");
    if (!json) {
        return json.error();
    }
    return ParseProxies(json.value());
}

core::Result<ProxyInfo> MihomoApiClient::GetProxy(std::string_view name) const {
    auto json = RequestJson(impl_->config, "GET", "/proxies/" + EncodePathSegment(name));
    if (!json) {
        return json.error();
    }
    return ParseProxyInfo(json.value());
}

core::Result<void> MihomoApiClient::SelectProxy(std::string_view group,
                                                std::string_view target) const {
    nlohmann::json body{{"name", std::string(target)}};
    auto response =
        Request(impl_->config, "PUT", "/proxies/" + EncodePathSegment(group), body.dump());
    if (!response) {
        return response.error();
    }
    return {};
}

core::Result<RuntimeConfiguration> MihomoApiClient::GetConfigs() const {
    auto json = RequestJson(impl_->config, "GET", "/configs");
    if (!json) {
        return json.error();
    }
    return ParseConfigs(json.value());
}

core::Result<void> MihomoApiClient::PatchConfigs(const nlohmann::json& patch) const {
    auto response = Request(impl_->config, "PATCH", "/configs", patch.dump());
    if (!response) {
        return response.error();
    }
    return {};
}

core::Result<RuleCollection> MihomoApiClient::GetRules() const {
    auto json = RequestJson(impl_->config, "GET", "/rules");
    if (!json) {
        return json.error();
    }
    return ParseRules(json.value());
}

core::Result<ConnectionsSnapshot> MihomoApiClient::GetConnections() const {
    auto json = RequestJson(impl_->config, "GET", "/connections");
    if (!json) {
        return json.error();
    }
    return ParseConnections(json.value());
}

core::Result<ProxyProviderCollection> MihomoApiClient::GetProxyProviders() const {
    auto json = RequestJson(impl_->config, "GET", "/providers/proxies");
    if (!json) {
        return json.error();
    }
    return ParseProxyProviders(json.value());
}

core::Result<RuleProviderCollection> MihomoApiClient::GetRuleProviders() const {
    auto json = RequestJson(impl_->config, "GET", "/providers/rules");
    if (!json) {
        return json.error();
    }
    return ParseRuleProviders(json.value());
}

}  // namespace clashyuka::mihomo
