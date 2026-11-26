// ClashYuka mihomo WebSocket 客户端
// 基于 IXWebSocket 封装，订阅 mihomo 的 /traffic、/logs、/connections 三个流式端点。
// 对应 Rust 版 core/clash/ws.rs（ClashConnectionsConnector）。
//
// mihomo 不支持单连接多端点订阅，故为每个端点创建独立的 ix::WebSocket 实例。
// 回调在 IXWebSocket 内部后台线程被调用，上层需自行处理线程同步。
//
// 多订阅者：每个端点支持多个订阅者，订阅返回 int 句柄，调用 Unsubscribe*(handle)
// 注销。这避免了多个页面（如 Dashboard 与 Connections）争用同一回调槽位导致互相覆盖。
//
// 用法：
//   clash_yuka::clash::WsClient client;
//   int h1 = client.SubscribeTraffic([](const Traffic& t) { ... });
//   int h2 = client.SubscribeConnections([](const std::vector<Connection>& c, int total) { ... });
//   client.Connect("ws://127.0.0.1:9090", "secret");
//   ...
//   client.UnsubscribeTraffic(h1);
//   client.UnsubscribeConnections(h2);
//
// 注意：内部持有 3 个 ix::WebSocket 与 mutex，不可拷贝或移动，请在使用处直接构造。
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "types.h"

namespace clash_yuka::clash {

class WsClient {
public:
    using TrafficCallback = std::function<void(const Traffic&)>;
    using LogCallback = std::function<void(const LogEntry&)>;
    using ConnectionsCallback =
        std::function<void(const std::vector<Connection>&, int total)>;

    WsClient();
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;
    WsClient(WsClient&&) = delete;
    WsClient& operator=(WsClient&&) = delete;

    // 建立到 mihomo 的 3 个 ws 端点连接。
    // base_url 形如 "ws://127.0.0.1:9090"，也接受 "http://" / "https://"（自动转换为 ws/wss）。
    // secret 同时通过 query string ?token= 与 Authorization: Bearer 头传递（与 Rust 版一致）。
    // 重复调用会先关闭已有连接再重建。
    void Connect(const std::string& base_url, const std::string& secret);

    // 关闭所有连接并清除连接参数。订阅者保留，可在下次 Connect 后继续生效。
    void Disconnect();

    // 是否处于连接/重连中状态：已调用 Connect 且未 Disconnect，且 3 个连接均未彻底关闭。
    bool IsConnected() const;

    // 订阅回调（线程安全，可在运行时调用）。返回 int 句柄，供 Unsubscribe* 注销使用。
    // 同一端点支持多个订阅者；每次端点消息到达时，所有订阅者按注册顺序依次被调用。
    int SubscribeTraffic(TrafficCallback cb);
    int SubscribeLog(LogCallback cb);
    int SubscribeConnections(ConnectionsCallback cb);

    // 注销订阅（线程安全）。handle 为对应 Subscribe* 返回的句柄；
    // 无效或已注销的句柄静默忽略。
    void UnsubscribeTraffic(int handle);
    void UnsubscribeLog(int handle);
    void UnsubscribeConnections(int handle);

private:
    struct ConnectionParams {
        std::string base_url;
        std::string secret;
    };

    // 成员函数指针：处理已解析的 JSON 负载
    using JsonHandler = void (WsClient::*)(const nlohmann::json&);

    // 为指定端点创建并启动一个 ix::WebSocket（局部所有权），绑定消息回调到 handler。
    // 返回的 socket 由调用方在 state_mutex_ 保护下移入成员，确保并发读 IsConnected 无竞争。
    std::unique_ptr<ix::WebSocket> CreateEndpoint(const std::string& base_url,
                                                  const std::string& secret,
                                                  const std::string& path,
                                                  JsonHandler handler);

    // 各端点的 JSON 处理：解析并分发到对应订阅者
    void HandleTraffic(const nlohmann::json& j);
    void HandleLog(const nlohmann::json& j);
    void HandleConnections(const nlohmann::json& j);

    // 将 base_url 规范化为 ws:// 或 wss:// 形式（去尾部斜杠，转换 http(s):// 前缀）
    static std::string NormalizeBaseUrl(const std::string& base_url);
    // 拼接 {ws_base}/{path}?token={encoded_secret}
    static std::string BuildUrl(const std::string& ws_base_url,
                                const std::string& path,
                                const std::string& secret);
    // 百分号 URL 编码（与 ApiClient::urlEncode 一致实现）
    static std::string UrlEncode(const std::string& value);

    std::unique_ptr<ix::WebSocket> traffic_socket_;
    std::unique_ptr<ix::WebSocket> logs_socket_;
    std::unique_ptr<ix::WebSocket> connections_socket_;

    // 订阅者列表：{handle, callback}。callback_mutex_ 保护并发订阅/注销/遍历。
    mutable std::mutex callback_mutex_;
    std::vector<std::pair<int, TrafficCallback>> traffic_cbs_;
    std::vector<std::pair<int, LogCallback>> log_cbs_;
    std::vector<std::pair<int, ConnectionsCallback>> connections_cbs_;
    int next_handle_ = 0;  // 单调递增的订阅句柄计数器

    mutable std::mutex state_mutex_;
    // 连接参数；nullopt 表示未连接（已 Disconnect 或从未 Connect）
    std::optional<ConnectionParams> params_;
};

} // namespace clash_yuka::clash
