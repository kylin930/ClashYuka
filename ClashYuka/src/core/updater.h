// ClashYuka 更新检查（Phase 10 Task 26）
//
// 异步检查 GitHub Release，通过回调在主线程通知 UI。
// 简化版：不比较版本号，有 release 即提示更新。
#pragma once

#include <functional>
#include <string>

namespace clash_yuka::core {

// 更新信息
struct UpdateInfo {
    bool has_update = false;
    std::string version;       // 最新版本号
    std::string release_url;   // 发布页面 URL
    std::string release_notes; // 更新日志
    std::string download_url;  // 下载链接（可选）
};

class Updater {
public:
    static Updater& Get();

    // 不可拷贝/移动
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    Updater(Updater&&) = delete;
    Updater& operator=(Updater&&) = delete;

    // 异步检查更新。result_cb 在主线程被调用（内部用 CallAfter）。
    // GitHub API: https://api.github.com/repos/<repo>/releases/latest
    // repo 默认 "clash-nyanpasu/clash-nyanpasu"（占位，后续替换为 ClashYuka 自有仓库）
    void CheckUpdate(std::function<void(const UpdateInfo&)> result_cb,
                     const std::string& repo = "clash-nyanpasu/clash-nyanpasu");

    // 打开发布页面（用系统默认浏览器）
    static void OpenReleasePage(const std::string& url);

private:
    Updater() = default;
    ~Updater() = default;

    // 解析 GitHub API JSON 响应为 UpdateInfo
    static UpdateInfo ParseReleaseJson(const std::string& json_str);
};

} // namespace clash_yuka::core
