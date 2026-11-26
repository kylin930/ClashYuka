// ClashYuka 内核版本管理
// 管理已安装内核的版本信息，包括查询、存储、比较版本号。
// 版本信息存储在 <resources_dir>/cores.json 中。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clash_yuka::clash {

// 已安装内核信息
struct InstalledCoreInfo {
    std::string name;           // "mihomo" / "mihomo-alpha"
    std::filesystem::path path;
    bool is_alpha = false;
    std::string version;        // 已安装版本，空表示未知
    bool installed = false;     // 文件是否存在
};

// 内核版本管理器（由 AppContext 持有）
class CoreManager {
public:
    CoreManager();

    // 列出所有已知内核的安装状态（mihomo / mihomo-alpha）
    std::vector<InstalledCoreInfo> ListCores() const;

    // 获取单个内核信息
    InstalledCoreInfo GetCoreInfo(const std::string& name) const;

    // 检查内核是否已安装（文件存在）
    bool IsInstalled(const std::string& name) const;

    // 获取内核可执行文件路径
    std::filesystem::path GetCorePath(const std::string& name) const;

    // 读取已安装内核版本（从 cores.json）
    std::string GetInstalledVersion(const std::string& name) const;

    // 保存内核版本到 cores.json
    void SaveInstalledVersion(const std::string& name, const std::string& version);

    // 通过运行 `<core> -v` 检测版本（返回空字符串表示失败）
    std::string DetectVersion(const std::string& name) const;

    // 比较版本号（semver），返回：1 if v1>v2, -1 if v1<v2, 0 if equal
    // 支持格式：v1.19.13、1.19.13、v1.19.13-alpha
    // alpha 版本 < 同版本稳定版
    static int CompareVersions(const std::string& v1, const std::string& v2);

private:
    // cores.json 路径：<resources_dir>/cores.json
    std::filesystem::path GetCoresJsonPath() const;

    // 加载 cores.json（文件不存在返回空 json 对象）
    nlohmann::json LoadCoresJson() const;

    // 保存 cores.json
    void SaveCoresJson(const nlohmann::json& j) const;
};

} // namespace clash_yuka::clash
