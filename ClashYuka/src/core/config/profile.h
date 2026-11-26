// ClashYuka 订阅配置（profiles.yaml）数据结构
// 对应 Rust 版 config/profile/{mod.rs, profiles.rs, item_type.rs, item/}
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace clash_yuka::config {

// 订阅类型枚举
enum class ProfileType {
    Remote,
    Local,
    Merge,
    Script,
};

// 订阅项类型与字符串互转
std::string ProfileTypeToString(ProfileType t);
std::optional<ProfileType> ProfileTypeFromString(const std::string& s);

// Remote 订阅选项
struct ProfileOption {
    std::optional<std::string> user_agent;
    std::optional<bool> with_proxy;
    std::optional<bool> self_proxy;
    // 订阅更新间隔（分钟），默认 120
    uint64_t update_interval = 120;
};

// 订阅信息（订阅头 Subscription-Userinfo）
struct SubscriptionInfo {
    uint64_t upload = 0;
    uint64_t download = 0;
    uint64_t total = 0;
    uint64_t expire = 0;
};

// 单个订阅配置
// 合并 Rust 中 Remote/Local/Merge/Script 四种 variant 的字段为单一结构，
// 通过 type 字段区分；不同类型的特有字段（如 url/option/script_type）
// 用 std::optional 表示，缺失即未设置。
struct Profile {
    // 公共字段
    std::string uid;
    std::string name;
    std::string file;
    std::optional<std::string> desc;
    uint64_t updated = 0;
    ProfileType type = ProfileType::Local;

    // Remote 特有
    std::optional<std::string> url;
    std::optional<ProfileOption> option;
    std::optional<SubscriptionInfo> extra;

    // Remote / Local 的处理链
    std::vector<std::string> chain;

    // Script 特有：javascript | lua
    std::optional<std::string> script_type;

    // Local 特有：符号链接路径（保留为字符串）
    std::optional<std::string> symlinks;
};

// profiles.yaml 集合
struct Profiles {
    // 当前激活的订阅 uid（兼容单值或多值）
    std::vector<std::string> current;
    // 全局处理链
    std::vector<std::string> chain;
    // clash 字段白名单
    std::vector<std::string> valid;
    // 订阅列表
    std::vector<Profile> items;

    // 默认值：valid = ["dns", "unified-delay", "tcp-concurrent"]
    static Profiles Default();

    // 从 profiles.yaml 读取（不存在返回 Default()）
    static Profiles Load(const std::filesystem::path& path);

    // 写回 profiles.yaml
    void Save(const std::filesystem::path& path) const;

    // ---- 增删改查 ----
    // 按 uid 查找（找不到返回 nullptr）
    Profile* Get(const std::string& uid);
    const Profile* Get(const std::string& uid) const;

    // 追加新订阅（uid 已存在则返回 false）
    bool Add(Profile item);

    // 删除指定 uid 的订阅；若删除的是 current 中的项，则尝试自动激活首个 Local/Remote
    bool Remove(const std::string& uid);

    // 用 patch 中的字段更新指定 uid 的订阅（uid 与 type 不可变）
    bool Update(const std::string& uid, const Profile& patch);

    // 设置当前激活订阅（不存在则忽略）
    void SetCurrent(const std::string& uid);
};

// 生成 11 位随机 uid（对应 Rust nanoid 简化版）
// prefix 取 "R"/"L"/"M"/"S" 分别对应 Remote/Local/Merge/Script
std::string GenerateProfileUid(ProfileType type);

} // namespace clash_yuka::config
