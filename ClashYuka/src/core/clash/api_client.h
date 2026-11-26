// ClashYuka mihomo RESTful API 客户端
// 基于 IXWebSocket 的 HttpClient 封装，对应 Rust 版 core/clash/api.rs
//
// 用法：
//   clash_yuka::clash::ApiClient client("http://127.0.0.1:9090", "secret");
//   auto version = client.GetVersion();
//   auto snapshot = client.GetProxySnapshot();
//
// 注意：ix::HttpClient 内部持有 mutex/socket，不可拷贝或移动，
// 因此 ApiClient 也不可拷贝/移动。请在使用处直接构造。
//
// 线程模型：每次请求使用独立的 ix::HttpClient（底层每次请求本来就会新建 socket），
// 避免某个长请求（如流式 /memory、整组测速）持有共享锁而阻塞其它请求。
// 调用方仍应把 ApiClient 调用放到后台线程，避免阻塞 GUI 主线程。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>

#include "types.h"

namespace clash_yuka::clash {

// 通用请求结果。
// error 保留 mihomo 返回的原始错误信息（响应体 message / 传输层错误），
// 不做模糊化转换，便于上层展示具体失败原因。
struct ApiResult {
    bool ok = false;
    int http_status = 0;
    std::string error;
};

// 单节点测速结果（GET /proxies/{name}/delay）
struct ProxyDelayResult {
    bool ok = false;
    int delay_ms = 0;
    int http_status = 0;
    std::string error;
};

// 代理组测速结果（GET /group/{name}/delay）
// delays 保留 mihomo 返回的「节点名 -> 延迟」映射（内部按名称查找，不依赖顺序）。
struct GroupDelayResult {
    bool ok = false;
    int http_status = 0;
    std::string error;
    std::vector<std::pair<std::string, int>> delays;
};

// 一次 /proxies 抓取的完整快照：组列表 + 全部节点（含 Provider 节点）。
struct ProxySnapshot {
    std::vector<ProxyGroup> groups;
    std::vector<Proxy> proxies;
};

class ApiClient {
public:
    // base_url 如 "http://127.0.0.1:9090"（不带末尾斜杠）
    // secret 为 external-controller 的密钥，可为空
    ApiClient(std::string base_url, std::string secret);

    // ---- 通用 HTTP 请求 ----

    // GET，返回解析后的 JSON；HTTP 非 2xx 或解析失败时返回 nullopt
    std::optional<nlohmann::json> Get(const std::string& path);

    // PUT，body 默认为空对象；成功返回 true
    bool Put(const std::string& path, const nlohmann::json& body = nlohmann::json::object());

    // DELETE，成功返回 true
    bool Delete(const std::string& path);

    // PATCH，body 默认为空对象；成功返回 true
    bool Patch(const std::string& path, const nlohmann::json& body = nlohmann::json::object());

    // ---- 业务接口（返回强类型） ----

    // GET /version
    Version GetVersion();

    // 拉取 /proxies 与 /providers/proxies，一次性解析出代理组与节点。
    // 代理组来源：GLOBAL.all 中带 all 字段的条目（保持配置顺序，含嵌套组），
    // GLOBAL 自身追加到末尾；GLOBAL 缺失时退化为扫描所有带 all 的条目。
    // 节点：/proxies 的全部条目 + 仅有 HTTP/File 型 Provider 提供的节点。
    ProxySnapshot GetProxySnapshot();

    // GET /proxies（平铺的所有节点，含 Provider 节点）
    std::vector<Proxy> GetProxies();

    // 代理组（Selector/URLTest/Fallback/LoadBalance/Relay 等，按配置顺序）
    std::vector<ProxyGroup> GetProxyGroups();

    // PUT /proxies/{group}  body: {"name": target}
    // 路径参数是「代理组名称」，请求体 name 是「目标节点/子组名称」。
    // 不按代理类型做任何过滤，是否允许切换由 mihomo 决定，错误原样返回。
    ApiResult SelectProxy(const std::string& group, const std::string& target);

    // GET /proxies/{name}/delay?timeout={timeout}&url={url}
    // 单节点测速；失败时保留 HTTP 状态与 mihomo 错误信息。
    ProxyDelayResult GetProxyDelay(const std::string& name, const std::string& url, int timeout);

    // GET /group/{name}/delay?timeout={timeout}&url={url}
    // 整组测速（mihomo 内部并行测试组内成员），不会改变用户当前选择。
    GroupDelayResult GetGroupDelay(const std::string& group, const std::string& url, int timeout);

    // GET /rules
    std::vector<Rule> GetRules();

    // GET /connections
    std::vector<Connection> GetConnections();

    // DELETE /connections/{id}
    bool CloseConnection(const std::string& id);

    // DELETE /connections
    bool CloseAllConnections();

    // GET /providers/proxies
    std::vector<Provider> GetProxyProviders();

    // GET /providers/rules
    std::vector<Provider> GetRuleProviders();

    // PUT /providers/{type}/{name}  type 为 "proxies" 或 "rules"
    bool UpdateProvider(const std::string& type, const std::string& name);

    // GET /providers/proxies/{name}/healthcheck
    bool HealthCheckProvider(const std::string& name);

    // GET /traffic（注意：traffic 通常走 WebSocket，此一次性 GET 可能无响应）
    Traffic GetTraffic();

    // GET /memory
    // 注意：mihomo 的 /memory 是流式接口（chunked，每秒推送），
    // 本方法只读取第一个完整对象后立即取消，避免长时间占用连接。
    Memory GetMemory();

    // GET /configs（返回原始 JSON 字符串）
    std::string GetConfig();

    // PATCH /configs
    bool PatchConfig(const nlohmann::json& patch);

    // PUT /configs  body: {"path": path}
    // 热重载运行时配置；失败时 error 保留 mihomo 原始信息（如 YAML 解析错误）。
    ApiResult ReloadConfig(const std::string& path);

private:
    // 统一执行请求：负责鉴权、超时、HTTP 状态码与错误信息提取。
    // out_body 非空时写入原始响应体（无论成功失败）。
    ApiResult Perform(const std::string& method, const std::string& path,
                      const std::string* body, std::string* out_body);

    // 构造带 Authorization 头与超时设置的请求参数
    ix::HttpRequestArgsPtr createRequestArgs(ix::HttpClient& client);

    // 拼接完整 URL：base_url_ + path
    std::string buildUrl(const std::string& path) const;

    // URL 编码（代理名可能含空格/中文等特殊字符）
    std::string urlEncode(const std::string& value) const;

    std::string base_url_;
    std::string secret_;
};

} // namespace clash_yuka::clash
