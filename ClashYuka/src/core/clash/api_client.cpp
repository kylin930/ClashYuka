// ClashYuka mihomo RESTful API 客户端实现
#include "api_client.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <string>
#include <unordered_set>

namespace clash_yuka::clash {

namespace {

// 判断 HTTP 状态码是否为 2xx
inline bool isOk(int statusCode) { return statusCode >= 200 && statusCode < 300; }

// 代理组类型集合判断（仅用于缺少 GLOBAL 时的兜底枚举）
bool isGroupType(const std::string& type) {
    return type == "Selector" || type == "URLTest" || type == "Fallback" ||
           type == "LoadBalance" || type == "Relay";
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 从响应中提取错误信息：优先 mihomo 的 {"message": "..."}，否则原始响应体。
// 不做模糊化，保留内核返回的真实原因。
std::string ExtractErrorMessage(const ix::HttpResponsePtr& resp) {
    if (!resp) return "HTTP 请求失败（无响应）";
    if (resp->statusCode == 0 && !resp->errorMsg.empty()) return resp->errorMsg;

    std::string body = Trim(resp->body);
    if (!body.empty()) {
        try {
            auto j = nlohmann::json::parse(body);
            if (j.is_object() && j.contains("message") && j["message"].is_string()) {
                return j["message"].get<std::string>();
            }
            if (j.is_string()) return j.get<std::string>();
        } catch (const nlohmann::json::exception&) {
            // 非 JSON：直接使用响应体
        }
        constexpr size_t kMaxErrorLen = 400;
        if (body.size() > kMaxErrorLen) body = body.substr(0, kMaxErrorLen) + "...";
        return body;
    }
    if (!resp->errorMsg.empty()) return resp->errorMsg;
    return "HTTP " + std::to_string(resp->statusCode);
}

// 解析单个代理条目；name 缺失时使用 JSON 对象的 key（mihomo 以 key 为准）
Proxy ParseProxyEntry(const std::string& name, const nlohmann::json& value) {
    Proxy p = value.get<Proxy>();
    if (p.name.empty()) p.name = name;
    return p;
}

// 解析带 all 字段的代理组条目
ProxyGroup ParseGroupEntry(const std::string& name, const nlohmann::json& value) {
    ProxyGroup g = value.get<ProxyGroup>();
    if (g.name.empty()) g.name = name;
    return g;
}

bool HasAllField(const nlohmann::json& value) {
    return value.is_object() && value.contains("all") && value["all"].is_array();
}

} // namespace

ApiClient::ApiClient(std::string base_url, std::string secret)
    : base_url_(std::move(base_url)), secret_(std::move(secret)) {
    // 去除末尾斜杠，避免拼接出双斜杠
    while (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

ix::HttpRequestArgsPtr ApiClient::createRequestArgs(ix::HttpClient& client) {
    auto args = client.createRequest();
    args->extraHeaders["Content-Type"] = "application/json";
    if (!secret_.empty()) {
        args->extraHeaders["Authorization"] = "Bearer " + secret_;
    }
    // 本地 API connect 通常 < 1ms；transfer 受响应体大小影响（大配置可能达数 MB），
    // 故 connect 保持 5 秒、transfer 放宽到 30 秒，避免大节点列表超时。
    args->connectTimeout = 5;
    args->transferTimeout = 30;
    return args;
}

std::string ApiClient::buildUrl(const std::string& path) const {
    // path 应以 '/' 开头
    if (path.empty() || path[0] == '/') return base_url_ + path;
    return base_url_ + "/" + path;
}

std::string ApiClient::urlEncode(const std::string& value) const {
    // 手写 URL 编码（百分号编码），避免依赖 ix::HttpClient::urlEncode（其非 const）。
    // 用于路径参数与查询参数：中文按 UTF-8 字节逐字节编码，空格编码为 %20。
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0x0F]);
        }
    }
    return result;
}

// ---- 统一请求 ----

ApiResult ApiClient::Perform(const std::string& method, const std::string& path,
                             const std::string* body, std::string* out_body) {
    ApiResult result;
    const std::string url = buildUrl(path);
    const std::string payload = body ? *body : std::string("{}");

    ix::HttpResponsePtr response;
    // 每次请求使用独立的 HttpClient：避免长请求（流式 /memory、整组测速）
    // 持有共享锁而阻塞其它请求。IXWebSocket 每次请求本就会新建 socket，
    // 因此这里没有额外的连接复用损失。
    ix::HttpClient client;
    auto args = createRequestArgs(client);
    if (method == "GET") {
        response = client.get(url, args);
    } else if (method == "PUT") {
        response = client.put(url, payload, args);
    } else if (method == "DELETE") {
        response = client.Delete(url, args);
    } else if (method == "PATCH") {
        response = client.patch(url, payload, args);
    } else {
        response = client.request(url, method, payload, args);
    }

    if (!response) {
        result.error = "HTTP 请求失败（无响应）";
        std::cerr << "[ApiClient] " << method << " " << path << " failed: null response"
                  << std::endl;
        return result;
    }

    result.http_status = response->statusCode;
    if (out_body) *out_body = response->body;

    if (isOk(response->statusCode)) {
        result.ok = true;
        return result;
    }

    result.error = ExtractErrorMessage(response);
    std::cerr << "[ApiClient] " << method << " " << path << " failed: "
              << result.http_status << " " << result.error << std::endl;
    return result;
}

// ---- 通用 HTTP 请求 ----

std::optional<nlohmann::json> ApiClient::Get(const std::string& path) {
    std::string body;
    auto result = Perform("GET", path, nullptr, &body);
    if (!result.ok) return std::nullopt;
    try {
        return nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[ApiClient] GET " << path << " JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

bool ApiClient::Put(const std::string& path, const nlohmann::json& body) {
    const std::string payload = body.dump();
    return Perform("PUT", path, &payload, nullptr).ok;
}

bool ApiClient::Delete(const std::string& path) {
    return Perform("DELETE", path, nullptr, nullptr).ok;
}

bool ApiClient::Patch(const std::string& path, const nlohmann::json& body) {
    const std::string payload = body.dump();
    return Perform("PATCH", path, &payload, nullptr).ok;
}

// ---- 业务接口 ----

Version ApiClient::GetVersion() {
    auto j = Get("/version");
    if (!j) return Version{};
    try {
        return j->get<Version>();
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[ApiClient] GetVersion deserialize error: " << e.what() << std::endl;
        return Version{};
    }
}

ProxySnapshot ApiClient::GetProxySnapshot() {
    ProxySnapshot snapshot;
    auto j = Get("/proxies");
    if (!j || !j->contains("proxies") || !(*j)["proxies"].is_object()) return snapshot;
    const auto& proxies = (*j)["proxies"];

    // 1. 解析全部节点/组条目（/proxies 的 key 即名称）
    std::unordered_set<std::string> known;
    known.reserve(proxies.size());
    for (const auto& [key, value] : proxies.items()) {
        try {
            snapshot.proxies.push_back(ParseProxyEntry(key, value));
            known.insert(key);
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetProxySnapshot skip item '" << key
                      << "': " << e.what() << std::endl;
        }
    }

    // 2. 代理组枚举：优先按 GLOBAL.all 的配置顺序（与 Nyanpasu 一致），
    //    只取其中带 all 字段的条目（即真正的策略组，含嵌套组）；
    //    GLOBAL 缺失时退化为扫描所有带 all / 组类型的条目。
    std::vector<std::string> group_order;
    std::unordered_set<std::string> group_seen;
    const auto global_it = proxies.find("GLOBAL");
    const bool has_global = global_it != proxies.end() && HasAllField(global_it.value());
    if (has_global) {
        for (const auto& item : global_it.value().at("all")) {
            if (!item.is_string()) continue;
            const std::string name = item.get<std::string>();
            if (name == "GLOBAL" || group_seen.count(name)) continue;
            auto it = proxies.find(name);
            if (it == proxies.end() || !HasAllField(it.value())) continue;
            group_seen.insert(name);
            group_order.push_back(name);
        }
    } else {
        for (const auto& [key, value] : proxies.items()) {
            if (key == "GLOBAL" || !HasAllField(value)) continue;
            if (value.contains("type") && value["type"].is_string() &&
                !isGroupType(value["type"].get<std::string>())) {
                continue;
            }
            group_seen.insert(key);
            group_order.push_back(key);
        }
    }

    // GLOBAL 自身也是可选组（用于 global 模式），追加到列表末尾
    if (has_global && !group_seen.count("GLOBAL")) {
        group_seen.insert("GLOBAL");
        group_order.push_back("GLOBAL");
    }

    for (const auto& name : group_order) {
        auto group_it = proxies.find(name);
        if (group_it == proxies.end()) continue;
        try {
            snapshot.groups.push_back(ParseGroupEntry(name, group_it.value()));
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetProxySnapshot skip group '" << name
                      << "': " << e.what() << std::endl;
        }
    }

    // 3. 合并 Proxy Provider 节点：部分 mihomo 版本的 /proxies 不返回 Provider
    //    内的节点，需从 /providers/proxies 补全（仅 HTTP/File 型，Compatible
    //    类型是内置组镜像，不能当节点合并）。
    auto providers = Get("/providers/proxies");
    if (providers && providers->contains("providers") &&
        (*providers)["providers"].is_object()) {
        for (const auto& [provider_name, provider] : (*providers)["providers"].items()) {
            if (!provider.is_object()) continue;
            std::string vehicle = provider.value("vehicleType", std::string());
            vehicle = toLower(vehicle);
            if (vehicle != "http" && vehicle != "file") continue;
            if (!provider.contains("proxies") || !provider["proxies"].is_array()) continue;
            for (const auto& item : provider["proxies"]) {
                if (!item.is_object()) continue;
                std::string name = item.value("name", std::string());
                if (name.empty() || known.count(name)) continue;
                try {
                    Proxy p = item.get<Proxy>();
                    if (p.name.empty()) p.name = name;
                    if (p.provider.empty()) p.provider = provider_name;
                    snapshot.proxies.push_back(std::move(p));
                    known.insert(name);
                } catch (const nlohmann::json::exception& e) {
                    std::cerr << "[ApiClient] GetProxySnapshot skip provider item '"
                              << name << "': " << e.what() << std::endl;
                }
            }
        }
    }

    return snapshot;
}

std::vector<Proxy> ApiClient::GetProxies() {
    return GetProxySnapshot().proxies;
}

std::vector<ProxyGroup> ApiClient::GetProxyGroups() {
    return GetProxySnapshot().groups;
}

ApiResult ApiClient::SelectProxy(const std::string& group, const std::string& target) {
    // 路径参数是代理组名称，请求体 name 是目标节点（或子组）名称。
    // 代理组/节点名称可能含中文、空格等，需逐段 URL 编码。
    nlohmann::json body = {{"name", target}};
    const std::string payload = body.dump();
    return Perform("PUT", "/proxies/" + urlEncode(group), &payload, nullptr);
}

ProxyDelayResult ApiClient::GetProxyDelay(const std::string& name,
                                          const std::string& url, int timeout) {
    ProxyDelayResult result;
    const std::string path = "/proxies/" + urlEncode(name) +
                             "/delay?timeout=" + std::to_string(timeout) +
                             "&url=" + urlEncode(url);

    std::string body;
    auto api_result = Perform("GET", path, nullptr, &body);
    result.http_status = api_result.http_status;
    result.error = api_result.error;
    if (!api_result.ok) return result;

    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.contains("delay") || !j["delay"].is_number()) {
            result.error = "响应缺少 delay 字段";
            return result;
        }
        const int delay = j["delay"].get<int>();
        if (delay <= 0) {
            result.error = "测速返回 " + std::to_string(delay) + "ms（节点不可用）";
            return result;
        }
        result.ok = true;
        result.delay_ms = delay;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("delay 响应解析失败: ") + e.what();
    }
    return result;
}

GroupDelayResult ApiClient::GetGroupDelay(const std::string& group,
                                          const std::string& url, int timeout) {
    GroupDelayResult result;
    const std::string path = "/group/" + urlEncode(group) +
                             "/delay?timeout=" + std::to_string(timeout) +
                             "&url=" + urlEncode(url);

    std::string body;
    auto api_result = Perform("GET", path, nullptr, &body);
    result.http_status = api_result.http_status;
    result.error = api_result.error;
    if (!api_result.ok) return result;

    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.is_object()) {
            result.error = "组测速响应不是 JSON 对象";
            return result;
        }
        // mihomo 返回 {"节点名": 延迟, ...}；失败节点可能不在结果中。
        for (const auto& [name, value] : j.items()) {
            if (!value.is_number()) continue;
            result.delays.emplace_back(name, value.get<int>());
        }
        result.ok = true;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("组测速响应解析失败: ") + e.what();
    }
    return result;
}

std::vector<Rule> ApiClient::GetRules() {
    std::vector<Rule> result;
    auto j = Get("/rules");
    if (!j || !j->contains("rules") || !(*j)["rules"].is_array()) return result;

    for (const auto& item : (*j)["rules"]) {
        try {
            result.push_back(item.get<Rule>());
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetRules skip item: " << e.what() << std::endl;
        }
    }
    return result;
}

std::vector<Connection> ApiClient::GetConnections() {
    std::vector<Connection> result;
    auto j = Get("/connections");
    if (!j || !j->contains("connections") || !(*j)["connections"].is_array()) return result;

    for (const auto& item : (*j)["connections"]) {
        try {
            result.push_back(item.get<Connection>());
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetConnections skip item: " << e.what() << std::endl;
        }
    }
    return result;
}

bool ApiClient::CloseConnection(const std::string& id) {
    return Delete("/connections/" + urlEncode(id));
}

bool ApiClient::CloseAllConnections() {
    return Delete("/connections");
}

std::vector<Provider> ApiClient::GetProxyProviders() {
    std::vector<Provider> result;
    auto j = Get("/providers/proxies");
    if (!j || !j->contains("providers") || !(*j)["providers"].is_object()) return result;

    for (const auto& [key, value] : (*j)["providers"].items()) {
        try {
            result.push_back(value.get<Provider>());
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetProxyProviders skip item '" << key
                      << "': " << e.what() << std::endl;
        }
    }
    return result;
}

std::vector<Provider> ApiClient::GetRuleProviders() {
    std::vector<Provider> result;
    auto j = Get("/providers/rules");
    if (!j || !j->contains("providers") || !(*j)["providers"].is_object()) return result;

    for (const auto& [key, value] : (*j)["providers"].items()) {
        try {
            result.push_back(value.get<Provider>());
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetRuleProviders skip item '" << key
                      << "': " << e.what() << std::endl;
        }
    }
    return result;
}

bool ApiClient::UpdateProvider(const std::string& type, const std::string& name) {
    // type 为 "proxies" 或 "rules"
    std::string path = "/providers/" + type + "/" + urlEncode(name);
    return Put(path);
}

bool ApiClient::HealthCheckProvider(const std::string& name) {
    std::string path = "/providers/proxies/" + urlEncode(name) + "/healthcheck";
    // 健康检查是 GET 请求，返回 200 表示触发成功
    return Get(path).has_value();
}

Traffic ApiClient::GetTraffic() {
    // /traffic 通常为 WebSocket 端点；一次性 GET 可能无响应或返回非 JSON
    auto j = Get("/traffic");
    if (!j) return Traffic{};
    try {
        return j->get<Traffic>();
    } catch (const nlohmann::json::exception&) {
        return Traffic{};
    }
}

Memory ApiClient::GetMemory() {
    // /memory 是流式接口：mihomo 返回 Transfer-Encoding: chunked，每秒推送一个
    // JSON 对象且不关闭连接。一次性 GET 会一直读到 transferTimeout（30s）并长期
    // 占用连接；因此这里用独立的 HttpClient + chunk 回调，拿到一个「非 0」的
    // 完整对象后立即取消后续读取（启动瞬间 mihomo 可能先推一个 inuse=0 的采样）。
    ix::HttpClient client;
    auto args = createRequestArgs(client);
    args->transferTimeout = 5;

    std::string pending;
    std::string latest_obj;
    std::atomic<bool>* cancel_flag = &args->cancel;
    args->onChunkCallback = [&pending, &latest_obj, cancel_flag](const std::string& chunk) {
        pending += chunk;
        // 从缓冲区中依次切出完整 JSON 对象，保留最后一个
        for (;;) {
            const size_t start = pending.find('{');
            if (start == std::string::npos) {
                pending.clear();
                return;
            }
            const size_t end = pending.find('}', start);
            if (end == std::string::npos) {
                if (start > 0) pending.erase(0, start);
                return;
            }
            latest_obj = pending.substr(start, end - start + 1);
            pending.erase(0, end + 1);

            try {
                const auto j = nlohmann::json::parse(latest_obj);
                if (j.value("inuse", 0) > 0) {
                    cancel_flag->store(true, std::memory_order_release);
                    return;
                }
            } catch (const nlohmann::json::exception&) {
                // 忽略不完整片段，继续等待
            }
        }
    };

    (void)client.get(buildUrl("/memory"), args);

    if (!latest_obj.empty()) {
        try {
            const auto j = nlohmann::json::parse(latest_obj);
            return j.get<Memory>();
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ApiClient] GetMemory parse error: " << e.what() << std::endl;
        }
    }
    return Memory{};
}

std::string ApiClient::GetConfig() {
    // 返回原始响应体（JSON 字符串）
    std::string body;
    auto result = Perform("GET", "/configs", nullptr, &body);
    if (!result.ok) return {};
    return body;
}

bool ApiClient::PatchConfig(const nlohmann::json& patch) {
    return Patch("/configs", patch);
}

ApiResult ApiClient::ReloadConfig(const std::string& path) {
    nlohmann::json body = {{"path", path}};
    const std::string payload = body.dump();
    return Perform("PUT", "/configs", &payload, nullptr);
}

} // namespace clash_yuka::clash
