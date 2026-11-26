// ClashYuka 内核下载器实现
#include "downloader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <system_error>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>

#include "core/app_context.h"
#include "core/resources.h"
#include "core/utils/dirs.h"

#ifdef _WIN32
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib, "winhttp.lib")
#endif

#if defined(__has_include) && __has_include(<wx/zipstrm.h>) && __has_include(<wx/wfstream.h>)
#  include <wx/zipstrm.h>
#  include <wx/wfstream.h>
#  define CLASHYUKA_HAS_WXZIP 1
#endif

namespace clash_yuka::clash {

namespace fs = std::filesystem;

namespace {

// 不区分大小写的子串包含判断
bool icontains(const std::string& s, const std::string& needle) {
    if (needle.empty()) return true;
    std::string ls = s, ln = needle;
    std::transform(ls.begin(), ls.end(), ls.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(ln.begin(), ln.end(), ln.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ls.find(ln) != std::string::npos;
}

// 不区分大小写的 ".zip" 后缀判断
bool endsWithZip(const std::string& s) {
    if (s.size() < 4) return false;
    std::string tail = s.substr(s.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".zip";
}

} // namespace

// ---- ApplyMirror ----
std::string CoreDownloader::ApplyMirror(const std::string& url, const std::string& mirror_url) {
    if (mirror_url.empty()) return url;

    // 去除镜像源末尾斜杠
    std::string m = mirror_url;
    while (!m.empty() && m.back() == '/') m.pop_back();

    // 替换 https://github.com -> mirror + /xxx
    const std::string gh = "https://github.com";
    if (url.find(gh) == 0) {
        return m + url.substr(gh.size());
    }

    // 替换 https://api.github.com -> mirror + //https://api.github.com/...
    // gh-proxy.com 等镜像格式要求完整保留原 URL 在 path 中
    const std::string api = "https://api.github.com";
    if (url.find(api) == 0) {
        return m + "/" + url;
    }

    // 替换 https://raw.githubusercontent.com -> mirror + /https://raw.githubusercontent.com/...
    // gh-proxy.com 支持 raw 文件加速
    const std::string raw = "https://raw.githubusercontent.com";
    if (url.find(raw) == 0) {
        return m + "/" + url;
    }

    // 替换 https://objects.githubusercontent.com（GitHub Desktop 资源）
    const std::string obj = "https://objects.githubusercontent.com";
    if (url.find(obj) == 0) {
        return m + "/" + url;
    }

    // 替换 https://gist.githubusercontent.com
    const std::string gist = "https://gist.githubusercontent.com";
    if (url.find(gist) == 0) {
        return m + "/" + url;
    }

    return url;
}

// ---- SelectAssetUrl ----
std::string CoreDownloader::SelectAssetUrl(const nlohmann::json& assets) {
    if (!assets.is_array()) return "";

    // 提取 asset 的 browser_download_url
    auto getUrl = [](const nlohmann::json& asset) -> std::string {
        if (!asset.is_object()) return "";
        if (!asset.contains("browser_download_url") ||
            !asset["browser_download_url"].is_string()) return "";
        return asset["browser_download_url"].get<std::string>();
    };

    // 规则 1: windows + amd64-v1 + .zip
    for (const auto& asset : assets) {
        if (!asset.contains("name") || !asset["name"].is_string()) continue;
        std::string name = asset["name"].get<std::string>();
        if (icontains(name, "windows") && icontains(name, "amd64-v1") && endsWithZip(name)) {
            auto url = getUrl(asset);
            if (!url.empty()) return url;
        }
    }

    // 规则 2: windows + amd64-v3 + .zip，不含 go123/go124
    for (const auto& asset : assets) {
        if (!asset.contains("name") || !asset["name"].is_string()) continue;
        std::string name = asset["name"].get<std::string>();
        if (icontains(name, "windows") && icontains(name, "amd64-v3") && endsWithZip(name) &&
            !icontains(name, "go123") && !icontains(name, "go124")) {
            auto url = getUrl(asset);
            if (!url.empty()) return url;
        }
    }

    // 规则 3: windows + amd64 + .zip，不含 compatible/go123/go124
    for (const auto& asset : assets) {
        if (!asset.contains("name") || !asset["name"].is_string()) continue;
        std::string name = asset["name"].get<std::string>();
        if (icontains(name, "windows") && icontains(name, "amd64") && endsWithZip(name) &&
            !icontains(name, "compatible") &&
            !icontains(name, "go123") && !icontains(name, "go124")) {
            auto url = getUrl(asset);
            if (!url.empty()) return url;
        }
    }

    return "";
}

// ---- CheckLatestVersion ----
std::optional<CoreVersionInfo> CoreDownloader::CheckLatestVersion(
    const std::string& core_name,
    const std::string& mirror_url) {

    // 1. 构造 API URL
    std::string api_url;
    if (core_name == "mihomo-alpha") {
        // Alpha 版需遍历 releases 列表，取首个 prerelease
        api_url = "https://api.github.com/repos/MetaCubeX/mihomo/releases";
    } else {
        api_url = "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest";
    }
    api_url = ApplyMirror(api_url, mirror_url);

    // 2. 发起 HTTP 请求
    ix::HttpClient client;
    auto args = client.createRequest();
    args->connectTimeout = 15;
    args->transferTimeout = 15;
    args->extraHeaders["Accept"] = "application/vnd.github+json";
    args->extraHeaders["User-Agent"] = "ClashYuka";

    auto response = client.get(api_url, args);
    if (!response || response->statusCode != 200) {
        std::cerr << "[CoreDownloader] CheckLatestVersion failed: ";
        if (response) {
            std::cerr << response->statusCode << " " << response->errorMsg;
        } else {
            std::cerr << "null response";
        }
        std::cerr << std::endl;
        return std::nullopt;
    }

    // 3. 解析 JSON
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(response->body);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[CoreDownloader] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    // 4. 对于 alpha，遍历数组取首个 prerelease
    if (core_name == "mihomo-alpha") {
        if (!j.is_array() || j.empty()) {
            std::cerr << "[CoreDownloader] alpha releases list empty" << std::endl;
            return std::nullopt;
        }
        const nlohmann::json* target = nullptr;
        for (const auto& item : j) {
            if (item.is_object() && item.contains("prerelease") &&
                item["prerelease"].is_boolean() && item["prerelease"].get<bool>()) {
                target = &item;
                break;
            }
        }
        if (!target) target = &j[0];  // 无 prerelease 时退化为首个
        j = *target;
    }

    if (!j.is_object()) {
        std::cerr << "[CoreDownloader] release JSON not an object" << std::endl;
        return std::nullopt;
    }

    // 5. 提取字段
    CoreVersionInfo info;
    try {
        if (j.contains("tag_name") && j["tag_name"].is_string()) {
            info.version = j["tag_name"].get<std::string>();
        }
        if (j.contains("body") && j["body"].is_string()) {
            info.release_notes = j["body"].get<std::string>();
        }
        if (j.contains("prerelease") && j["prerelease"].is_boolean()) {
            info.is_prerelease = j["prerelease"].get<bool>();
        }
        if (j.contains("assets") && j["assets"].is_array()) {
            info.download_url = SelectAssetUrl(j["assets"]);
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[CoreDownloader] JSON field extract error: " << e.what() << std::endl;
        return std::nullopt;
    }

    // 6. 应用镜像源到 download_url
    info.download_url = ApplyMirror(info.download_url, mirror_url);

    if (info.version.empty() || info.download_url.empty()) {
        std::cerr << "[CoreDownloader] version or download_url empty" << std::endl;
        return std::nullopt;
    }

    return info;
}

// ---- DownloadFile (Windows WinHTTP 实现) ----
bool CoreDownloader::DownloadFile(
    const std::string& url,
    const std::filesystem::path& dest,
    std::function<void(const DownloadProgress&)> progress_cb) {
#ifdef _WIN32
    // 1. 解析 URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    // URL 通常为 ASCII，按字符扩展为宽字符串
    std::wstring wurl;
    wurl.reserve(url.size());
    for (char c : url) wurl.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));

    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &urlComp)) {
        std::cerr << "[CoreDownloader] WinHttpCrackUrl failed: " << GetLastError() << std::endl;
        return false;
    }

    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    std::wstring extraInfo;
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        extraInfo.assign(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    INTERNET_PORT port = urlComp.nPort;
    bool is_https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    // 拼接 path + extra info 作为请求路径
    std::wstring requestPath = urlPath + extraInfo;

    // 2. 打开 WinHTTP 会话
    HINTERNET hSession = WinHttpOpen(L"ClashYuka/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "[CoreDownloader] WinHttpOpen failed: " << GetLastError() << std::endl;
        return false;
    }

    // 3. 连接服务器
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        std::cerr << "[CoreDownloader] WinHttpConnect failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 4. 创建请求
    DWORD openFlags = WINHTTP_FLAG_REFRESH;
    if (is_https) openFlags |= WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", requestPath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, openFlags);
    if (!hRequest) {
        std::cerr << "[CoreDownloader] WinHttpOpenRequest failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 5. 发送请求
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        std::cerr << "[CoreDownloader] WinHttpSendRequest failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 6. 接收响应
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        std::cerr << "[CoreDownloader] WinHttpReceiveResponse failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 7. 查询状态码
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
        std::cerr << "[CoreDownloader] query status code failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    if (statusCode != 200) {
        std::cerr << "[CoreDownloader] HTTP " << statusCode << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 8. 查询 Content-Length（可能不存在，返回 0 表示未知）
    size_t total = 0;
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        total = contentLength;
    }

    // 9. 打开输出文件（确保父目录存在）
    {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
    }
    std::ofstream ofs(dest, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "[CoreDownloader] cannot open dest file: " << dest.string() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 10. 循环读取数据并写入文件
    constexpr DWORD kBufSize = 64 * 1024;
    std::vector<char> buf(kBufSize);
    size_t downloaded = 0;
    bool read_ok = true;

    while (true) {
        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), kBufSize, &bytes_read)) {
            std::cerr << "[CoreDownloader] WinHttpReadData failed: " << GetLastError() << std::endl;
            read_ok = false;
            break;
        }
        if (bytes_read == 0) break;  // 读取完毕

        ofs.write(buf.data(), static_cast<std::streamsize>(bytes_read));
        if (!ofs) {
            std::cerr << "[CoreDownloader] write file failed" << std::endl;
            read_ok = false;
            break;
        }

        downloaded += bytes_read;

        // 进度回调
        if (progress_cb) {
            DownloadProgress p;
            p.downloaded = downloaded;
            p.total = total;
            if (total > 0) {
                p.percent = static_cast<int>(downloaded * 100 / total);
                if (p.percent > 100) p.percent = 100;
            } else {
                p.percent = -1;
            }
            progress_cb(p);
        }
    }

    ofs.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return read_ok;
#else
    // 非 Windows 平台暂未实现
    (void)url; (void)dest; (void)progress_cb;
    std::cerr << "[CoreDownloader] DownloadFile only implemented on Windows" << std::endl;
    return false;
#endif
}

// ---- ExtractZip ----
bool CoreDownloader::ExtractZip(
    const std::filesystem::path& zip_path,
    const std::filesystem::path& dest_dir) {

    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) {
        std::cerr << "[CoreDownloader] create extract dir failed: " << ec.message() << std::endl;
        return false;
    }

#ifdef CLASHYUKA_HAS_WXZIP
    // 优先尝试 wxZipInputStream
    try {
        wxFFileInputStream in(wxString(zip_path.wstring()));
        if (in.IsOk()) {
            wxZipInputStream zip(in);
            if (zip.IsOk()) {
                bool extracted_any = false;
                std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());
                while (entry) {
                    wxString name = entry->GetName();
                    std::string sname(name.ToUTF8().data());

                    // 只提取可执行文件（名称含 "mihomo" 且不含 ".txt"）
                    if (!entry->IsDir() &&
                        icontains(sname, "mihomo") &&
                        sname.find(".txt") == std::string::npos) {

                        fs::path out_path = dest_dir / sname;
                        fs::create_directories(out_path.parent_path(), ec);

                        wxFFileOutputStream out(wxString(out_path.wstring()));
                        if (out.IsOk()) {
                            zip.Read(out);
                            out.Close();
                            extracted_any = true;
                        } else {
                            std::cerr << "[CoreDownloader] cannot create output: "
                                      << out_path.string() << std::endl;
                        }
                    }
                    entry.reset(zip.GetNextEntry());
                }
                if (extracted_any) return true;
                std::cerr << "[CoreDownloader] wxZip extracted no files" << std::endl;
            } else {
                std::cerr << "[CoreDownloader] wxZipInputStream init failed" << std::endl;
            }
        } else {
            std::cerr << "[CoreDownloader] wxFFileInputStream open failed" << std::endl;
        }
        // wx 失败则继续走 fallback
    } catch (const std::exception& e) {
        std::cerr << "[CoreDownloader] wxZip extract exception: " << e.what() << std::endl;
    }
#endif

    // 回退：使用系统命令 tar -xf（Windows 10+ 自带 bsdtar）
    std::string cmd = "tar -xf \"" + zip_path.string() + "\" -C \"" + dest_dir.string() + "\"";
    std::cerr << "[CoreDownloader] fallback to: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[CoreDownloader] tar extract failed: " << ret << std::endl;
        return false;
    }
    return true;
}

// ---- DownloadAndInstall ----
std::filesystem::path CoreDownloader::DownloadAndInstall(
    const std::string& core_name,
    const CoreVersionInfo& info,
    std::function<void(const DownloadProgress&)> progress_cb,
    const std::string& mirror_url) {

    std::error_code ec;

    // 1. 获取 resources 目录，并确保存在
    fs::path resources_dir = core::Resources::Get().GetResourcesDir();
    core::Resources::Get().EnsureResourcesDir();

    // 2. 创建临时下载目录
    fs::path tmp_dir = resources_dir / ".download_tmp";
    fs::create_directories(tmp_dir, ec);
    if (ec) {
        std::cerr << "[CoreDownloader] create tmp dir failed: " << ec.message() << std::endl;
        return {};
    }

    // 3. 应用镜像源到 download_url
    std::string download_url = ApplyMirror(info.download_url, mirror_url);

    // 4. 下载 zip 到临时目录
    fs::path zip_path = tmp_dir / (core_name + ".zip");
    if (!DownloadFile(download_url, zip_path, progress_cb)) {
        std::cerr << "[CoreDownloader] download failed" << std::endl;
        fs::remove_all(tmp_dir, ec);
        return {};
    }

    // 5. 停止当前内核（如正在运行）
    try {
        if (AppContext::Get().IsInitialized()) {
            auto& proc = AppContext::Get().process();
            if (proc.IsRunning()) {
                proc.Stop();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CoreDownloader] stop process exception: " << e.what() << std::endl;
    }

    // 6. 确定目标内核文件名
#ifdef _WIN32
    std::string target_name = core_name + ".exe";
#else
    std::string target_name = core_name;
#endif
    fs::path target_path = resources_dir / target_name;
    fs::path backup_path = resources_dir / (target_name + ".bak");

    // 7. 备份旧内核文件（如果存在）
    bool has_backup = false;
    if (fs::exists(target_path, ec) && fs::is_regular_file(target_path, ec)) {
        fs::remove(backup_path, ec);  // 删除可能残留的旧备份
        fs::rename(target_path, backup_path, ec);
        if (ec) {
            std::cerr << "[CoreDownloader] backup failed: " << ec.message() << std::endl;
            fs::remove_all(tmp_dir, ec);
            return {};
        }
        has_backup = true;
#ifdef _WIN32
        // 等待文件系统刷新（Windows 上原文件可能仍被占用）
        Sleep(500);
#endif
    }

    // 8. 解压 zip 到临时目录
    fs::path extract_dir = tmp_dir / "extracted";
    fs::remove_all(extract_dir, ec);
    fs::create_directories(extract_dir, ec);
    if (!ExtractZip(zip_path, extract_dir)) {
        std::cerr << "[CoreDownloader] extract failed" << std::endl;
        if (has_backup) {
            fs::rename(backup_path, target_path, ec);
        }
        fs::remove_all(tmp_dir, ec);
        return {};
    }

    // 9. 在解压目录中查找内核可执行文件
    fs::path found_exe;
    std::string lower_target = target_name;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // 优先按名称精确匹配
    for (const auto& entry : fs::recursive_directory_iterator(extract_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string fname = entry.path().filename().string();
        std::string lower_fname = fname;
        std::transform(lower_fname.begin(), lower_fname.end(), lower_fname.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower_fname == lower_target) {
            found_exe = entry.path();
            break;
        }
    }

    // 模糊匹配：含 "mihomo" 的可执行文件
    if (found_exe.empty()) {
        for (const auto& entry : fs::recursive_directory_iterator(extract_dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            std::string fname = entry.path().filename().string();
            std::string lower_fname = fname;
            std::transform(lower_fname.begin(), lower_fname.end(), lower_fname.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (icontains(lower_fname, "mihomo")) {
#ifdef _WIN32
                if (lower_fname.size() < 4 ||
                    lower_fname.compare(lower_fname.size() - 4, 4, ".exe") != 0) {
                    continue;
                }
#endif
                found_exe = entry.path();
                break;
            }
        }
    }

    if (found_exe.empty()) {
        std::cerr << "[CoreDownloader] no core executable found in extracted dir" << std::endl;
        if (has_backup) {
            fs::rename(backup_path, target_path, ec);
        }
        fs::remove_all(tmp_dir, ec);
        return {};
    }

    // 10. 移动到目标位置
    fs::rename(found_exe, target_path, ec);
    if (ec) {
        // rename 可能因跨卷失败，回退到 copy + remove
        fs::copy_file(found_exe, target_path, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[CoreDownloader] move to target failed: " << ec.message() << std::endl;
            if (has_backup) {
                fs::rename(backup_path, target_path, ec);
            }
            fs::remove_all(tmp_dir, ec);
            return {};
        }
        fs::remove(found_exe, ec);
    }

    // 11. 验证新内核文件
    bool ok = fs::exists(target_path, ec) &&
              fs::is_regular_file(target_path, ec) &&
              fs::file_size(target_path, ec) > 0;
    if (ok) {
        // 验证成功：删除备份
        if (has_backup) {
            fs::remove(backup_path, ec);
        }
    } else {
        // 验证失败：恢复备份
        std::cerr << "[CoreDownloader] verify new core failed" << std::endl;
        fs::remove(target_path, ec);
        if (has_backup) {
            fs::rename(backup_path, target_path, ec);
        }
    }

    // 12. 清理临时目录
    fs::remove_all(tmp_dir, ec);

    return ok ? target_path : fs::path{};
}

// ============================================================
// GeoIP / GeoSite 数据集下载
// 数据源：MetaCubeX/meta-rules-dat（mihomo 官方配套规则集）
// Asset 命名：Country.mmdb / geosite.dat
// ============================================================

// 从 assets 列表中选出指定 name 的 browser_download_url，未找到返回空
static std::string FindAssetByName(const nlohmann::json& assets, const std::string& name) {
    if (!assets.is_array()) return "";
    for (const auto& asset : assets) {
        if (!asset.is_object()) continue;
        if (!asset.contains("name") || !asset["name"].is_string()) continue;
        if (asset["name"].get<std::string>() != name) continue;
        if (!asset.contains("browser_download_url") ||
            !asset["browser_download_url"].is_string()) continue;
        return asset["browser_download_url"].get<std::string>();
    }
    return "";
}

std::optional<GeoAssetInfo> CoreDownloader::CheckLatestGeoAssets(
    const std::string& mirror_url) {

    // 1. 构造 API URL
    std::string api_url = "https://api.github.com/repos/MetaCubeX/meta-rules-dat/releases/latest";
    api_url = ApplyMirror(api_url, mirror_url);

    // 2. 发起 HTTP 请求
    ix::HttpClient client;
    auto args = client.createRequest();
    args->connectTimeout = 15;
    args->transferTimeout = 30;
    args->extraHeaders["Accept"] = "application/vnd.github+json";
    args->extraHeaders["User-Agent"] = "ClashYuka";

    auto response = client.get(api_url, args);
    if (!response || response->statusCode != 200) {
        std::cerr << "[CoreDownloader] CheckLatestGeoAssets failed: ";
        if (response) {
            std::cerr << response->statusCode << " " << response->errorMsg;
        } else {
            std::cerr << "null response";
        }
        std::cerr << std::endl;
        return std::nullopt;
    }

    // 3. 解析 JSON
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(response->body);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[CoreDownloader] GeoAssets JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    if (!j.is_object()) {
        std::cerr << "[CoreDownloader] GeoAssets release JSON not an object" << std::endl;
        return std::nullopt;
    }

    // 4. 提取字段
    GeoAssetInfo info;
    try {
        if (j.contains("tag_name") && j["tag_name"].is_string()) {
            info.version = j["tag_name"].get<std::string>();
        }
        if (j.contains("body") && j["body"].is_string()) {
            info.release_notes = j["body"].get<std::string>();
        }
        if (j.contains("assets") && j["assets"].is_array()) {
            info.geoip_url = FindAssetByName(j["assets"], "Country.mmdb");
            info.geosite_url = FindAssetByName(j["assets"], "geosite.dat");
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[CoreDownloader] GeoAssets JSON field extract error: " << e.what() << std::endl;
        return std::nullopt;
    }

    // 5. 应用镜像源到 download_url
    info.geoip_url = ApplyMirror(info.geoip_url, mirror_url);
    info.geosite_url = ApplyMirror(info.geosite_url, mirror_url);

    if (info.version.empty() || info.geoip_url.empty()) {
        std::cerr << "[CoreDownloader] GeoAssets version or geoip_url empty" << std::endl;
        return std::nullopt;
    }

    return info;
}

// 通用 Geo 文件下载实现：原子写入到目标路径，成功后返回
std::filesystem::path CoreDownloader::DownloadGeoFile(
    const std::string& url,
    const std::filesystem::path& dest,
    std::function<void(const DownloadProgress&)> progress_cb) {
    if (url.empty()) {
        std::cerr << "[CoreDownloader] DownloadGeoFile: empty url" << std::endl;
        return {};
    }

    // 确保父目录存在
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);

    // 下载到临时文件，成功后原子重命名
    fs::path tmp = dest;
    tmp += ".tmp";

    if (!DownloadFile(url, tmp, progress_cb)) {
        std::cerr << "[CoreDownloader] DownloadGeoFile failed: " << url << std::endl;
        fs::remove(tmp, ec);
        return {};
    }

    // 验证下载文件非空
    std::error_code ec2;
    if (!fs::exists(tmp, ec2) || fs::file_size(tmp, ec2) <= 0) {
        std::cerr << "[CoreDownloader] DownloadGeoFile: empty file" << std::endl;
        fs::remove(tmp, ec);
        return {};
    }

    // 原子替换（先删旧文件再重命名）
    if (fs::exists(dest, ec)) {
        fs::remove(dest, ec);
    }
    fs::rename(tmp, dest, ec);
    if (ec) {
        // rename 失败则 copy 后清理
        fs::copy_file(tmp, dest, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) {
            std::cerr << "[CoreDownloader] Geo file replace failed: " << ec.message() << std::endl;
            return {};
        }
    }
    return dest;
}

std::filesystem::path CoreDownloader::DownloadGeoIP(
    const GeoAssetInfo& info,
    std::function<void(const DownloadProgress&)> progress_cb,
    const std::string& mirror_url) {
    (void)mirror_url;  // info.geoip_url 已应用镜像
    core::Resources::Get().EnsureResourcesDir();
    fs::path dest = core::Resources::Get().GetGeoIPPath();
    return DownloadGeoFile(info.geoip_url, dest, progress_cb);
}

std::filesystem::path CoreDownloader::DownloadGeoSite(
    const GeoAssetInfo& info,
    std::function<void(const DownloadProgress&)> progress_cb,
    const std::string& mirror_url) {
    (void)mirror_url;  // info.geosite_url 已应用镜像
    core::Resources::Get().EnsureResourcesDir();
    fs::path dest = core::Resources::Get().GetGeoSitePath();
    return DownloadGeoFile(info.geosite_url, dest, progress_cb);
}

} // namespace clash_yuka::clash
