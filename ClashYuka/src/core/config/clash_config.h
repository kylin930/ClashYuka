// ClashYuka Clash 配置（clash-guard-overrides.yaml 与运行时 IClash）数据结构
// 对应 Rust 版 config/clash/mod.rs 的 IClash / IClashTemp / ClashInfo / DNS / TUN
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace clash_yuka::config {

// ---- kebab-case 字段（与 clash 配置一致）----

struct TUN {
    std::optional<bool> enable;
    std::optional<std::string> stack;          // system | gvisor | mixed
    std::optional<bool> auto_route;
    std::optional<bool> auto_detect_interface;
    std::optional<std::vector<std::string>> dns_hijack;
};

struct FallbackFilter {
    std::optional<bool> geoip;
    std::optional<std::string> geoip_code;
    std::optional<std::vector<std::string>> ipcidr;
    std::optional<std::vector<std::string>> domain;
};

struct DNS {
    std::optional<bool> enable;
    std::optional<std::string> listen;
    std::optional<std::vector<std::string>> default_nameserver;
    std::optional<std::string> enhanced_mode;       // fake-ip | redir-host
    std::optional<std::string> fake_ip_range;
    std::optional<bool> use_hosts;
    std::optional<std::vector<std::string>> fake_ip_filter;
    std::optional<std::vector<std::string>> nameserver;
    std::optional<std::vector<std::string>> fallback;
    std::optional<FallbackFilter> fallback_filter;
    std::optional<std::vector<std::string>> nameserver_policy;
};

// IClash：结构化 clash 配置（kebab-case YAML）
struct IClash {
    std::optional<uint16_t> mixed_port;
    std::optional<bool> allow_lan;
    std::optional<std::string> log_level;       // debug | info | warning | error | silent
    std::optional<bool> ipv6;
    std::optional<std::string> mode;            // rule | global | direct
    std::optional<std::string> external_controller;
    std::optional<std::string> secret;
    std::optional<DNS> dns;
    std::optional<TUN> tun;
    std::optional<std::string> interface_name;

    static IClash LoadFromYaml(const YAML::Node& root);
    YAML::Node ToYaml() const;
};

// 运行时连接信息
struct ClashInfo {
    // clash core port（mixed-port）
    uint16_t port = 0;
    // external-controller 地址，例如 "127.0.0.1:9090"
    std::string server;
    // clash secret
    std::optional<std::string> secret;
};

// IClashTemp：clash-guard-overrides.yaml 的原始 Mapping 表
// 保留为 YAML::Node 形式以便任意键值读写；guard 保证 mixed-port 与
// external-controller 始终有效。
class IClashTemp {
public:
    IClashTemp() = default;
    // 用已存在的 YAML 节点构造（不会自动 guard，调用方负责）
    explicit IClashTemp(YAML::Node data) : data_(std::move(data)) {}

    // 从 clash-guard-overrides.yaml 读取（不存在或失败时返回 Template()）
    static IClashTemp Load(const std::filesystem::path& path);

    // 写回 clash-guard-overrides.yaml
    void Save(const std::filesystem::path& path) const;

    // 默认模板（对应 Rust template()，含 mixed-port=7890、external-controller、secret 等）
    static IClashTemp Template();

    // patch：用 patch 节点中的键覆盖 data 中的键
    void PatchConfig(const YAML::Node& patch);

    // 计算客户端连接信息
    ClashInfo GetClientInfo() const;

    // 获取 mixed-port（缺失或 0 返回 7890）
    uint16_t GetMixedPort() const;

    const YAML::Node& data() const { return data_; }
    YAML::Node& data() { return data_; }

    // ---- guard 静态工具 ----
    static uint16_t GuardMixedPort(const YAML::Node& config);
    static std::string GuardServerCtrl(const YAML::Node& config);
    static std::string GuardClientCtrl(const YAML::Node& config);
    static YAML::Node Guard(YAML::Node config);

private:
    YAML::Node data_;
};

} // namespace clash_yuka::config
