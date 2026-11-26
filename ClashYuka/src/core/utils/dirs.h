// ClashYuka 跨平台目录定位
// 对应 Rust 版 utils/dirs.rs 的子集（去除 tauri/winreg/portable 复杂分支）
#pragma once

#include <filesystem>
#include <string>

namespace clash_yuka::utils {

// 应用名称（小写用于 Unix 路径，显示名用于 Windows APPDATA）
inline constexpr const char* kAppName = "clash-yuka";
inline constexpr const char* kAppNameDisplay = "ClashYuka";

// 配置文件名常量
inline constexpr const char* kVergeConfigFile = "verge.yaml";
inline constexpr const char* kProfilesYamlFile = "profiles.yaml";
inline constexpr const char* kClashGuardOverridesFile = "clash-guard-overrides.yaml";

// 应用配置目录：verge.yaml / profiles.yaml / clash-guard-overrides.yaml 存放处。
// 目录不存在时自动创建。
// Windows: %APPDATA%\ClashYuka
// macOS  : ~/Library/Application Support/clash-yuka
// Linux  : $XDG_CONFIG_HOME/clash-yuka 或 ~/.config/clash-yuka
std::filesystem::path app_config_dir();

// 应用数据目录：profiles/、logs/、cache 等存放处。目录不存在时自动创建。
// Windows: %LOCALAPPDATA%\ClashYuka
// macOS  : 与 config_dir 相同（Application Support）
// Linux  : $XDG_DATA_HOME/clash-yuka 或 ~/.local/share/clash-yuka
std::filesystem::path app_data_dir();

// 日志目录：<data_dir>/logs，不存在时自动创建。
std::filesystem::path app_log_dir();

// 资源目录（mihomo 等核心二进制）：默认 <exe_dir>/resources，不自动创建。
std::filesystem::path app_resources_dir();

// 派生路径
std::filesystem::path verge_config_path();            // <config>/verge.yaml
std::filesystem::path profiles_yaml_path();           // <config>/profiles.yaml
std::filesystem::path profiles_dir();                 // <data>/profiles
std::filesystem::path clash_guard_overrides_path();   // <config>/clash-guard-overrides.yaml

} // namespace clash_yuka::utils
