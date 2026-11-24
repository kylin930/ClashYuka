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

core::Result<std::string> Request(const MihomoApiConfig& config, const char* method,
                                  const std::string& path, const std::string& body = {},
                                  const char* content_type = nullptr, bool boundedClose = false,
                                  int totalTimeoutMs = 0) {
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

    if (boundedClose) {
        client.set_follow_location(false);
        client.set_max_timeout(5000);
        client.set_payload_max_length(64 * 1024);
    }
    if (totalTimeoutMs > 0) {
        client.set_max_timeout(totalTimeoutMs);
    }

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
    } else if (verb == "DELETE") {
        result = client.Delete(path);
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
        return MakeError(ErrorKind::HttpStatus, "unexpected HTTP status " + std::to_string(status),
                         status);
    }

    return result->body;
}

core::Result<nlohmann::json> RequestJson(const MihomoApiConfig& config, const char* method,
                                         const std::string& path,
                                         const std::string& body = {}, int totalTimeoutMs = 0) {
    auto response = Request(config, method, path, body, nullptr, false, totalTimeoutMs);
    if (!response) {
        return response.error();
    }

    try {
        return nlohmann::json::parse(response.value());
    } catch (const nlohmann::json::exception&) {
        // JSON exception messages can include snippets of the response.
        return MakeError(ErrorKind::InvalidJson, "response is not valid JSON");
    }
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

core::Result<void> MihomoApiClient::ClearProxySelection(std::string_view group) const {
    auto response = Request(impl_->config, "DELETE", "/proxies/" + EncodePathSegment(group));
    if (!response) {
        return response.error();
    }
    return {};
}

core::Result<int> MihomoApiClient::TestProxyDelay(std::string_view name, std::string_view url,
                                                  int timeoutMs) const {
    if (url.empty()) {
        return MakeError(ErrorKind::InvalidConfig, "delay test URL must not be empty");
    }
    if (timeoutMs <= 0 || timeoutMs > 60000) {
        return MakeError(ErrorKind::InvalidConfig, "delay test timeout is out of range");
    }
    const std::string path = "/proxies/" + EncodePathSegment(name) + "/delay?url=" +
                             EncodeQueryComponent(url) + "&timeout=" +
                             std::to_string(timeoutMs);
    auto json = RequestJson(impl_->config, "GET", path);
    if (!json) {
        core::Error error = json.error();
        if (error.httpStatus == 408 || error.httpStatus == 504) {
            error.kind = ErrorKind::Timeout;
            error.message = "delay test timed out";
        }
        return error;
    }
    const auto it = json.value().find("delay");
    if (it == json.value().end() || !it->is_number()) {
        return MakeError(ErrorKind::MissingField, "delay response has no numeric 'delay'");
    }
    return static_cast<int>(it->get<double>());
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
    auto json = RequestJson(impl_->config, "GET", "/rules", {}, impl_->config.readTimeoutMs);
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

core::Result<void> MihomoApiClient::CloseConnection(std::string_view id) const {
    auto config = impl_->config;
    if (!IsLoopbackHost(config.host) || config.secret.empty()) {
        return MakeError(ErrorKind::InvalidConfig,
                         "closing a connection requires an authenticated loopback controller");
    }
    // Resolve the accepted loopback aliases locally without consulting DNS.
    if (config.host == "[::1]") {
        config.host = "::1";
    } else if (config.host != "::1") {
        config.host = "127.0.0.1";
    }
    // Reject IDs that could normalize to a different route after decoding.
    if (id.empty() || id.find_first_not_of('/') == std::string_view::npos) {
        return MakeError(ErrorKind::InvalidConfig, "invalid connection ID");
    }
    for (auto rest = id; !rest.empty();) {
        const auto separator = rest.find('/');
        const auto segment = rest.substr(0, separator);
        if (segment == "." || segment == "..") {
            return MakeError(ErrorKind::InvalidConfig, "invalid connection ID");
        }
        if (separator == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(separator + 1);
    }
    auto response = Request(config, "DELETE", "/connections/" + EncodePathSegment(id), {}, nullptr, true);
    if (!response) {
        return response.error();
    }
    return {};
}

core::Result<ProxyProviderCollection> MihomoApiClient::GetProxyProviders() const {
    auto json = RequestJson(impl_->config, "GET", "/providers/proxies");
    if (!json) {
        return json.error();
    }
    return ParseProxyProviders(json.value());
}

core::Result<RuleProviderCollection> MihomoApiClient::GetRuleProviders() const {
    auto json = RequestJson(impl_->config, "GET", "/providers/rules", {}, impl_->config.readTimeoutMs);
    if (!json) {
        return json.error();
    }
    return ParseRuleProviders(json.value());
}

core::Result<void> MihomoApiClient::UpdateRuleProvider(std::string_view name) const {
    if (name.empty() || name.find('\0') != std::string_view::npos ||
        name.find_first_not_of('/') == std::string_view::npos) {
        return MakeError(ErrorKind::InvalidConfig, "invalid rule provider name");
    }
    for (auto rest = name; !rest.empty();) {
        const auto separator = rest.find('/');
        const auto segment = rest.substr(0, separator);
        if (segment == "." || segment == "..") {
            return MakeError(ErrorKind::InvalidConfig, "invalid rule provider name");
        }
        if (separator == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(separator + 1);
    }
    auto response = Request(impl_->config, "PUT", "/providers/rules/" + EncodePathSegment(name),
                            {}, nullptr, true, impl_->config.readTimeoutMs);
    if (!response) {
        return response.error();
    }
    return {};
}

}  // namespace clashyuka::mihomo
