// ClashYuka 更新检查实现
#include "core/updater.h"

#include <wx/app.h>
#include <wx/string.h>
#include <wx/utils.h>  // wxLaunchDefaultBrowser

#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>

#include <thread>
#include <utility>

namespace clash_yuka::core {

Updater& Updater::Get() {
    static Updater instance;
    return instance;
}

void Updater::CheckUpdate(std::function<void(const UpdateInfo&)> result_cb,
                           const std::string& repo) {
    std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";

    // 在 detached 线程中发起 HTTP 请求，避免阻塞主线程
    std::thread([url, cb = std::move(result_cb)]() {
        ix::HttpClient client;
        auto args = client.createRequest();
        args->connectTimeout = 15;
        args->transferTimeout = 15;
        args->extraHeaders["Accept"] = "application/vnd.github+json";
        args->extraHeaders["User-Agent"] = "ClashYuka";

        auto response = client.get(url, args);

        UpdateInfo info;
        if (response && response->statusCode == 200) {
            info = ParseReleaseJson(response->body);
        }

        // 通过 CallAfter 在主线程回调
        if (wxTheApp) {
            wxTheApp->CallAfter([info, cb]() {
                if (cb) cb(info);
            });
        }
    }).detach();
}

UpdateInfo Updater::ParseReleaseJson(const std::string& json_str) {
    UpdateInfo info;
    try {
        auto j = nlohmann::json::parse(json_str);
        // tag_name 存在即视为有更新（简化版不比较版本号）
        if (j.contains("tag_name") && j["tag_name"].is_string()) {
            info.version = j["tag_name"].get<std::string>();
            info.has_update = true;
        }
        if (j.contains("html_url") && j["html_url"].is_string()) {
            info.release_url = j["html_url"].get<std::string>();
        }
        if (j.contains("body") && j["body"].is_string()) {
            info.release_notes = j["body"].get<std::string>();
        }
        // assets 数组的第一个元素即为下载链接
        if (j.contains("assets") && j["assets"].is_array() && !j["assets"].empty()) {
            const auto& asset = j["assets"][0];
            if (asset.is_object() &&
                asset.contains("browser_download_url") &&
                asset["browser_download_url"].is_string()) {
                info.download_url = asset["browser_download_url"].get<std::string>();
            }
        }
    } catch (...) {
        // JSON 解析失败，has_update 保持 false
    }
    return info;
}

void Updater::OpenReleasePage(const std::string& url) {
    if (url.empty()) return;
    wxLaunchDefaultBrowser(wxString::FromUTF8(url));
}

} // namespace clash_yuka::core
