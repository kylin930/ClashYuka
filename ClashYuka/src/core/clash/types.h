// ClashYuka mihomo RESTful API 强类型数据结构
// 对应 Rust 版 core/clash/api.rs 中的数据结构与 core/clash/proxies.rs 的代理组结构
//
// 字段使用 snake_case 命名；mihomo API 返回的 camelCase 字段（如 sourceIP、
// vehicleType、updatedAt 等）在 from_json 中做映射。bool 型字段（alive、meta、
// premium）按任务要求转为字符串。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clash_yuka::clash {

// 代理节点（对应 mihomo /proxies 中的单个条目）
struct Proxy {
    std::string name;
    std::string type;                     // Shadowsocks/VMess/Trojan/Direct/Reject/...
    std::optional<int> delay;             // 最近测速延迟 ms（从 history 末尾提取）
    std::string alive;                    // "alive"/"dead"（API 返回 bool，转换）
    std::string provider;                 // 来源 Proxy Provider 名称（无则为空）
    std::vector<std::string> history;     // 历史延迟（简化为 delay 字符串列表）
};

// 代理组（type 为 Selector/URLTest/Fallback/LoadBalance/Relay 等）
struct ProxyGroup {
    std::string name;
    std::string type;
    std::string now;                      // 当前选中节点
    std::vector<std::string> all;         // 全部节点名
};

// 规则
struct Rule {
    std::string type;                     // RuleSet/DOMAIN/IP-CIDR/...
    std::string payload;
    std::string proxy;                    // 目标代理/策略组名
};

// 连接（/connections 返回的单个连接）
struct Connection {
    std::string id;
    std::optional<std::string> network;   // tcp/udp（来自 metadata）
    std::optional<std::string> type;
    std::optional<std::string> host;
    std::optional<std::string> sourceIP;          // API: sourceIP (camelCase)
    std::optional<std::string> sourcePort;
    std::optional<std::string> destinationPort;   // API: destinationPort
    std::optional<std::string> destinationIP;     // API: destinationIP
    std::optional<std::string> chains;            // 代理链（API 返回数组，合并为逗号分隔）
    std::optional<int64_t> upload;
    std::optional<int64_t> download;
    std::optional<std::string> rule;
    std::optional<std::string> rulePayload;       // API: rulePayload (camelCase)
    std::optional<int64_t> start;
};

// 日志条目
struct LogEntry {
    std::string type;                     // INFO/WARNING/ERROR/DEBUG/...
    std::string payload;
};

// 代理/规则提供者
struct Provider {
    std::string name;
    std::string type;                     // Proxy/Rule
    std::string vehicleType;              // API: vehicleType (camelCase) - HTTP/File/Compatible
    std::optional<std::string> updatedAt; // API: updatedAt (camelCase)
    std::optional<int> subscriptionInfo;  // API: subscriptionInfo (camelCase，简化为 int)
};

// 流量
struct Traffic {
    int64_t up = 0;
    int64_t down = 0;
};

// 内存
struct Memory {
    int64_t inuse = 0;
    std::optional<int64_t> oslimit;
};

// 版本信息（meta/premium 在 API 中为 bool，此处按任务要求转为字符串）
struct Version {
    std::string version;
    std::optional<std::string> meta;
    std::optional<std::string> premium;
};

// ---- 简单结构使用宏自动生成 from_json（缺失字段使用默认值，多余字段忽略）----
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProxyGroup, name, type, now, all)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Rule, type, payload, proxy)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LogEntry, type, payload)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Traffic, up, down)

// ---- 复杂结构（含 camelCase 字段映射或类型转换或 std::optional 字段）手写 from_json，实现见 types.cpp ----
// 注意：含 std::optional 字段的结构不能用宏（宏生成的 to_json 不支持 optional），
// 这里只需从 mihomo 反序列化，手写 from_json 即可。
void from_json(const nlohmann::json& j, Proxy& p);
void from_json(const nlohmann::json& j, Connection& c);
void from_json(const nlohmann::json& j, Provider& p);
void from_json(const nlohmann::json& j, Version& v);
void from_json(const nlohmann::json& j, Memory& m);

} // namespace clash_yuka::clash
