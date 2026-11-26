// ClashYuka 运行时配置生成器
// 对应 Rust 版 enhance/mod.rs 的 enhance() 与 config/core.rs 的 generate()
#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "core/config/profile.h"
#include "core/config/verge_config.h"

namespace clash_yuka::enhance {

// 构建结果
struct BuildResult {
    // 是否成功生成并写入文件
    bool ok = false;
    // 最终配置包含的键（小写）
    std::vector<std::string> exists_keys;
    // 处理过程中的日志/错误信息
    std::vector<std::string> logs;
};

// 运行时 config.yaml 生成器
//
// 根据 profiles.current 与 enhance chain 生成 mihomo 运行时配置：
//   1. 取 profiles.current 对应 profile 文件（Remote/Local）作为基础配置；
//   2. 对每个 current profile 应用其自身 chain（profile.chain）中的 enhance 项；
//   3. 合并多个 current profile（首个完整作为基础，其余仅合并 proxies 列表）；
//   4. 应用全局 chain（profiles.chain）中的 enhance 项；
//   5. 字段过滤：enable_clash_fields 为 true 时移除 ClashYuka 自管理的字段；
//   6. 写入 output_path。
//
// Script 类型 enhance 本阶段仅记录日志并跳过（阶段7接入 QuickJS/LuaJIT）。
class ConfigBuilder {
public:
    ConfigBuilder();
    ~ConfigBuilder();

    ConfigBuilder(const ConfigBuilder&) = delete;
    ConfigBuilder& operator=(const ConfigBuilder&) = delete;

    // 设置 verge 配置（用于读取 enable_clash_fields 等开关）。
    // 未设置时使用默认值（enable_clash_fields = true）。
    void SetVerge(const clash_yuka::config::IVerge& verge);

    // 生成运行时配置并写入 output_path。
    // profiles 为 profiles.yaml 解析后的结构；profiles_dir 为 profile 文件所在目录。
    // 失败时 result.ok=false 且 logs 中记录原因。
    BuildResult Build(clash_yuka::config::Profiles& profiles,
                      const std::filesystem::path& profiles_dir,
                      const std::filesystem::path& output_path);

private:
    // 读取 profile 文件并返回 YAML 节点；失败返回空节点。
    YAML::Node LoadProfileYaml(const std::filesystem::path& file_path);

    // 应用一条 chain（按顺序合并 Merge 类型，跳过 Script 类型）。
    void ApplyChain(YAML::Node& config,
                    const clash_yuka::config::Profiles& profiles,
                    const std::vector<std::string>& chain,
                    const std::filesystem::path& profiles_dir,
                    std::vector<std::string>& logs);

    // 字段过滤：enable_clash_fields 为 true 时移除自管理字段。
    void ApplyFieldFilter(YAML::Node& config, bool enable_clash_fields);

    clash_yuka::config::IVerge verge_;
    bool verge_set_ = false;
};

} // namespace clash_yuka::enhance
