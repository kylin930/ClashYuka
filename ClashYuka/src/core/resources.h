// ClashYuka 资源管理（Phase 11 Task 29）
//
// 管理应用外部资源文件：mihomo 核心、服务模式 helper、GeoIP 数据库等。
// 资源文件统一放置在 <app_dir>/resources/ 目录下
// （macOS 位于 .app/Contents/Resources/）。
//
// 资源目录解析顺序：
//   1. utils::app_resources_dir()（<exe_dir>/resources，正式部署路径）
//   2. macOS bundle：<exe_dir>/../Resources
//   3. 开发环境回退：相对可执行文件向上查找的 resources/ 目录
//      （用于未将资源拷贝到构建输出目录的开发构建）
//
// 该类为 Meyers singleton（static 局部变量），首次 Get() 调用时构造。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace clash_yuka::core {

class Resources {
public:
    static Resources& Get();

    // 禁止拷贝/移动
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) = delete;
    Resources& operator=(Resources&&) = delete;

    // 获取资源根目录（<app_dir>/resources 或开发环境的源码 resources/ 目录）。
    const std::filesystem::path& GetResourcesDir() const;

    // 获取 mihomo 核心可执行文件路径。
    std::filesystem::path GetMihomoPath() const;
    // 获取 mihomo alpha 核心路径。
    std::filesystem::path GetMihomoAlphaPath() const;
    // 获取 clash-rs 核心路径。
    std::filesystem::path GetClashRsPath() const;

    // 获取服务模式 helper 路径（Windows: clash-yuka-service.exe）。
    std::filesystem::path GetServiceHelperPath() const;

    // 获取 GeoIP 数据库路径（Country.mmdb）。
    std::filesystem::path GetGeoIPPath() const;
    // 获取 GeoSite 数据库路径（geosite.dat）。
    std::filesystem::path GetGeoSitePath() const;

    // 列出所有可用的 clash 核心（扫描 resources 目录，检查已知核心文件名）。
    struct CoreEntry {
        std::string name;       // "mihomo", "mihomo-alpha", "clash-rs", "clash-rs-alpha"
        std::filesystem::path path;
        bool is_alpha = false;
    };
    std::vector<CoreEntry> ListAvailableCores() const;

    // 确保资源目录存在（首次运行时创建）。失败不抛异常。
    void EnsureResourcesDir() const;

private:
    Resources();
    ~Resources() = default;

    // 解析资源目录：按上述顺序查找首个存在的候选目录，全部不存在时回退到
    // utils::app_resources_dir()（正式部署路径）。
    static std::filesystem::path ResolveResourcesDir();

    std::filesystem::path resources_dir_;
};

} // namespace clash_yuka::core
