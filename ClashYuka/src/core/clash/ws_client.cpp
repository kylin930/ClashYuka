// ClashYuka mihomo WebSocket 客户端实现
#include "ws_client.h"

#include <iostream>
#include <utility>

namespace clash_yuka::clash {

namespace {

// 重连退避上限（毫秒）。IXWebSocket 内置指数退避：waitTime = 2^retryCount * 100ms，
// 并以此值封顶。序列约为 100ms/200ms/400ms/800ms/1.6s/3.2s/6.4s/12.8s/25.6s/30s…，
// 满足任务要求的“指数退避、上限 30s”。（自动重连默认已开启，此处显式启用以明示意。）
constexpr uint32_t kMaxReconnectWaitMs = 30000;

} // namespace

WsClient::WsClient() = default;

WsClient::~WsClient() {
    Disconnect();
}

// ---- 连接管理 ----

void WsClient::Connect(const std::string& base_url, const std::string& secret) {
    // 重复 Connect 先清理旧连接，避免泄漏 socket
    Disconnect();

    // 在局部变量中创建并启动 socket（start 非阻塞，回调仅依赖 this 与 callback_mutex_，
    // 不依赖成员变量，故在移入成员前触发回调也是安全的）
    auto traffic = CreateEndpoint(base_url, secret, "traffic", &WsClient::HandleTraffic);
    auto logs = CreateEndpoint(base_url, secret, "logs", &WsClient::HandleLog);
    auto conns = CreateEndpoint(base_url, secret, "connections",
                                &WsClient::HandleConnections);

    // 在锁下移入成员，确保并发 IsConnected 读取 unique_ptr 无数据竞争
    std::lock_guard<std::mutex> lock(state_mutex_);
    params_ = ConnectionParams{base_url, secret};
    traffic_socket_ = std::move(traffic);
    logs_socket_ = std::move(logs);
    connections_socket_ = std::move(conns);
}

void WsClient::Disconnect() {
    // 在锁下把 socket 移出成员（与 IsConnected 的读取互斥），然后在锁外执行 stop()。
    // stop() 同步等待 IXWebSocket 后台线程退出；回调只用 callback_mutex_、不申请
    // state_mutex_，故锁外 stop 不会死锁，也不阻塞并发的 IsConnected。
    std::unique_ptr<ix::WebSocket> traffic, logs, conns;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        traffic = std::move(traffic_socket_);
        logs = std::move(logs_socket_);
        conns = std::move(connections_socket_);
        params_.reset();
    }
    if (traffic) traffic->stop();
    if (logs) logs->stop();
    if (conns) conns->stop();
    // 局部 unique_ptr 在此析构，释放 socket
}

bool WsClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!params_.has_value()) return false;

    // 三个连接均存在且未进入 Closed 状态（Open 或 Connecting/Closing 均视为“在线/重连中”）
    auto alive = [](const std::unique_ptr<ix::WebSocket>& s) {
        return s && s->getReadyState() != ix::ReadyState::Closed;
    };
    return alive(traffic_socket_) && alive(logs_socket_) && alive(connections_socket_);
}

// ---- 订阅管理 ----

int WsClient::SubscribeTraffic(TrafficCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    int handle = ++next_handle_;
    traffic_cbs_.emplace_back(handle, std::move(cb));
    return handle;
}

int WsClient::SubscribeLog(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    int handle = ++next_handle_;
    log_cbs_.emplace_back(handle, std::move(cb));
    return handle;
}

int WsClient::SubscribeConnections(ConnectionsCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    int handle = ++next_handle_;
    connections_cbs_.emplace_back(handle, std::move(cb));
    return handle;
}

void WsClient::UnsubscribeTraffic(int handle) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto it = traffic_cbs_.begin(); it != traffic_cbs_.end(); ++it) {
        if (it->first == handle) {
            traffic_cbs_.erase(it);
            return;
        }
    }
}

void WsClient::UnsubscribeLog(int handle) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto it = log_cbs_.begin(); it != log_cbs_.end(); ++it) {
        if (it->first == handle) {
            log_cbs_.erase(it);
            return;
        }
    }
}

void WsClient::UnsubscribeConnections(int handle) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto it = connections_cbs_.begin(); it != connections_cbs_.end(); ++it) {
        if (it->first == handle) {
            connections_cbs_.erase(it);
            return;
        }
    }
}

// ---- 端点创建 ----

std::unique_ptr<ix::WebSocket> WsClient::CreateEndpoint(const std::string& base_url,
                                                         const std::string& secret,
                                                         const std::string& path,
                                                         JsonHandler handler) {
    auto socket = std::make_unique<ix::WebSocket>();

    const std::string ws_base = NormalizeBaseUrl(base_url);
    const std::string url = BuildUrl(ws_base, path, secret);
    socket->setUrl(url);

    // secret 鉴权：除 query string 外再附 Authorization 头（与 Rust 版 endpoint() 一致）
    ix::WebSocketHttpHeaders headers;
    if (!secret.empty()) {
        headers["Authorization"] = "Bearer " + secret;
    }
    socket->setExtraHeaders(headers);

    // 自动重连：IXWebSocket 内置退避，封顶 30s
    socket->enableAutomaticReconnection();
    socket->setMaxWaitBetweenReconnectionRetries(kMaxReconnectWaitMs);

    // 消息回调在 IXWebSocket 后台线程触发：解析 JSON 后调用对应 handler
    socket->setOnMessageCallback([this, handler](const ix::WebSocketMessagePtr& msg) {
        if (msg->type != ix::WebSocketMessageType::Message) {
            return;
        }
        // mihomo 推送文本帧；WebSocketMessage::str 始终持有负载（binary 仅标志帧类型）
        if (msg->str.empty()) return;

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(msg->str);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[WsClient] JSON parse error: " << e.what() << std::endl;
            return;
        }
        (this->*handler)(j);
    });

    socket->start();
    return socket;
}

// ---- JSON 分发 ----

void WsClient::HandleTraffic(const nlohmann::json& j) {
    Traffic traffic;
    try {
        traffic = j.get<Traffic>();
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[WsClient] traffic deserialize error: " << e.what() << std::endl;
        return;
    }

    // 在锁内拷贝一份订阅者列表，锁外逐个调用，避免回调中再次订阅/注销导致死锁
    std::vector<TrafficCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        snapshot.reserve(traffic_cbs_.size());
        for (auto& kv : traffic_cbs_) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& cb : snapshot) {
        if (cb) cb(traffic);
    }
}

void WsClient::HandleLog(const nlohmann::json& j) {
    LogEntry entry;
    try {
        entry = j.get<LogEntry>();
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[WsClient] log deserialize error: " << e.what() << std::endl;
        return;
    }

    std::vector<LogCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        snapshot.reserve(log_cbs_.size());
        for (auto& kv : log_cbs_) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& cb : snapshot) {
        if (cb) cb(entry);
    }
}

void WsClient::HandleConnections(const nlohmann::json& j) {
    std::vector<Connection> conns;
    int total = 0;

    try {
        // mihomo /connections 返回 {downloadTotal, uploadTotal, memory, connections:[...]}
        // 此处按任务约定取 total（优先读 "total" 字段，缺省回退为 connections 数组长度）
        total = j.value("total", 0);
        if (j.contains("connections") && j["connections"].is_array()) {
            conns = j["connections"].get<std::vector<Connection>>();
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[WsClient] connections deserialize error: " << e.what() << std::endl;
        return;
    }

    if (total <= 0) total = static_cast<int>(conns.size());

    std::vector<ConnectionsCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        snapshot.reserve(connections_cbs_.size());
        for (auto& kv : connections_cbs_) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& cb : snapshot) {
        if (cb) cb(conns, total);
    }
}

// ---- URL 工具 ----

std::string WsClient::NormalizeBaseUrl(const std::string& base_url) {
    std::string url = base_url;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    // http(s):// -> ws(s)://
    if (url.rfind("http://", 0) == 0) {
        url.replace(0, 7, "ws://");
    } else if (url.rfind("https://", 0) == 0) {
        url.replace(0, 8, "wss://");
    }
    // 已是 ws:// 或 wss:// 则保持不变
    return url;
}

std::string WsClient::BuildUrl(const std::string& ws_base_url,
                               const std::string& path,
                               const std::string& secret) {
    std::string url = ws_base_url + "/" + path;
    if (!secret.empty()) {
        url += "?token=" + UrlEncode(secret);
    }
    return url;
}

std::string WsClient::UrlEncode(const std::string& value) {
    // 与 ApiClient::urlEncode 一致的手写百分号编码
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

} // namespace clash_yuka::clash
