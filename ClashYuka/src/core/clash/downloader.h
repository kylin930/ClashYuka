// ClashYuka 内核下载器
//
// 提供 mihomo / mihomo-alpha 内核的版本查询、下载、解压、安装能力。
// 下载源支持 GitHub 官方 + 自定义镜像源（前缀替换）。
// Windows 端使用 WinHTTP API 实现带进度回调的 HTTPS 下载；
// 解压优先使用 wxZipInputStream，失败时回退到系统 tar 命令。
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace clash_yuka::clash {

// 内核版本信息（来自 GitHub Release API）
struct CoreVersionInfo {
    std::string version;        // tag_name，如 "v1.18.10"
    std::string download_url;   // 已选中的 asset browser_download_url（已应用镜像源）
    std::string release_notes;  // release body
    bool is_prerelease = false; // 是否为预发布（alpha）
};

// GeoIP/GeoSite 数据集信息（来自 MetaCubeX/meta-rules-dat Release）
// Country.mmdb 供 mihomo GEOIP 规则使用；geosite.dat 供 GEOSITE 规则使用。
struct GeoAssetInfo {
    std::string version;       // tag_name，如 "202412091049"
    std::string geoip_url;     // Country.mmdb 下载 URL（已应用镜像源）
    std::string geosite_url;   // geosite.dat 下载 URL（已应用镜像源）
    std::string release_notes; // release body（可选）
};

// 下载进度
struct DownloadProgress {
    int64_t downloaded = 0;  // 已下载字节数
    int64_t total = 0;       // 总字节数（未知时为 0）
    int percent = -1;        // 百分比 0-100；未知时为 -1
};

// 内核下载器（全部为静态方法，无需实例化）
class CoreDownloader {
public:
    // 查询指定内核的最新版本。
    // core_name: "mihomo" 或 "mihomo-alpha"
    // mirror_url: 镜像源前缀（空则直连 GitHub）
    // 失败返回 std::nullopt。
    static std::optional<CoreVersionInfo> CheckLatestVersion(
        const std::string& core_name,
        const std::string& mirror_url);

    // 下载并安装内核到 resources 目录。
    // 流程：临时下载 → 停止运行中内核 → 备份 → 解压 → 验证 → 清理。
    // 成功返回内核可执行文件路径，失败返回空 path。
    static std::filesystem::path DownloadAndInstall(
        const std::string& core_name,
        const CoreVersionInfo& info,
        std::function<void(const DownloadProgress&)> progress_cb,
        const std::string& mirror_url);

    // ---- GeoIP / GeoSite 数据集 ----
    // 数据源：MetaCubeX/meta-rules-dat（mihomo 官方配套规则集）

    // 查询最新 GeoIP/GeoSite 数据集版本。
    // mirror_url: 镜像源前缀（空则直连 GitHub）。
    // 失败返回 std::nullopt。
    static std::optional<GeoAssetInfo> CheckLatestGeoAssets(
        const std::string& mirror_url);

    // 下载 Country.mmdb 到 resources 目录。
    // 成功返回文件路径，失败返回空 path。
    // 若目标文件已存在则覆盖。
    static std::filesystem::path DownloadGeoIP(
        const GeoAssetInfo& info,
        std::function<void(const DownloadProgress&)> progress_cb,
        const std::string& mirror_url);

    // 下载 geosite.dat 到 resources 目录。
    // 成功返回文件路径，失败返回空 path。
    // 若目标文件已存在则覆盖。
    static std::filesystem::path DownloadGeoSite(
        const GeoAssetInfo& info,
        std::function<void(const DownloadProgress&)> progress_cb,
        const std::string& mirror_url);

private:
    // 对 URL 应用镜像源前缀替换
    static std::string ApplyMirror(const std::string& url, const std::string& mirror_url);

    // 从 GitHub Release assets 中按优先级选中匹配的下载 URL
    // 规则：amd64-v1 > amd64-v3（排除 go123/go124）> amd64（排除 compatible/go123/go124）
    static std::string SelectAssetUrl(const nlohmann::json& assets);

    // 下载单个文件到 dest（Windows 使用 WinHTTP）
    static bool DownloadFile(
        const std::string& url,
        const std::filesystem::path& dest,
        std::function<void(const DownloadProgress&)> progress_cb);

    // 下载 Geo 文件（Country.mmdb / geosite.dat）到 dest，原子替换已有文件
    static std::filesystem::path DownloadGeoFile(
        const std::string& url,
        const std::filesystem::path& dest,
        std::function<void(const DownloadProgress&)> progress_cb);

    // 解压 zip 到 dest_dir（优先 wxZipInputStream，回退 tar）
    static bool ExtractZip(
        const std::filesystem::path& zip_path,
        const std::filesystem::path& dest_dir);
};

} // namespace clash_yuka::clash
