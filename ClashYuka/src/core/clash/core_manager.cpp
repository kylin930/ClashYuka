// ClashYuka 内核版本管理实现
//
// 管理已安装内核（mihomo / mihomo-alpha）的版本信息。
// 版本数据持久化到 <resources_dir>/cores.json，支持通过运行 `<core> -v` 检测实际版本。
#include "core/clash/core_manager.h"

#include "core/resources.h"
#include "core/utils/dirs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#endif

namespace clash_yuka::clash {

namespace fs = std::filesystem;

namespace {

// 已知内核清单（仅 mihomo 系列，不含 clash-rs）
struct KnownCore {
    const char* name;
    const char* base;
    bool is_alpha;
};

const KnownCore kKnownCores[] = {
    {"mihomo",       "mihomo",       false},
    {"mihomo-alpha", "mihomo-alpha", true},
};

// 从 `core -v` 输出中提取版本号。
// 输出格式示例：Mihomo Meta v1.19.13 windows amd64 with go1.25
// 提取结果：v1.19.13
// 优先使用正则匹配，失败时回退到字符串查找。
std::string ExtractVersion(const std::string& output) {
    // 正则匹配：v 后跟 数字.数字.数字，可选预发布标签 -xxx
    try {
        std::regex re(R"(v(\d+\.\d+\.\d+(?:-\w+)?))");
        std::smatch m;
        if (std::regex_search(output, m, re)) {
            return m[0].str();
        }
    } catch (...) {
        // 正则异常，回退到字符串查找
    }
    // 回退：查找 'v' 后跟数字的子串（v 须位于词首）
    for (size_t i = 0; i + 1 < output.size(); ++i) {
        bool at_boundary = (i == 0 || output[i - 1] == ' ' ||
                            output[i - 1] == '\t' || output[i - 1] == '\n' ||
                            output[i - 1] == '\r');
        if (output[i] == 'v' && at_boundary &&
            std::isdigit(static_cast<unsigned char>(output[i + 1]))) {
            size_t start = i;
            size_t end = i + 1;
            // 消费数字和点
            while (end < output.size() &&
                   (std::isdigit(static_cast<unsigned char>(output[end])) ||
                    output[end] == '.')) {
                ++end;
            }
            // 消费预发布标签 -xxx
            if (end < output.size() && output[end] == '-') {
                ++end;
                while (end < output.size() &&
                       (std::isalnum(static_cast<unsigned char>(output[end])) ||
                        output[end] == '-')) {
                    ++end;
                }
            }
            return output.substr(start, end - start);
        }
    }
    return {};
}

} // namespace

CoreManager::CoreManager() = default;

fs::path CoreManager::GetCorePath(const std::string& name) const {
    if (name == "mihomo") {
        return core::Resources::Get().GetMihomoPath();
    }
    if (name == "mihomo-alpha") {
        return core::Resources::Get().GetMihomoAlphaPath();
    }
    return {};
}

bool CoreManager::IsInstalled(const std::string& name) const {
    fs::path p = GetCorePath(name);
    if (p.empty()) return false;
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

std::vector<InstalledCoreInfo> CoreManager::ListCores() const {
    std::vector<InstalledCoreInfo> result;
    result.reserve(sizeof(kKnownCores) / sizeof(kKnownCores[0]));
    for (const auto& kc : kKnownCores) {
        InstalledCoreInfo info;
        info.name = kc.name;
        info.is_alpha = kc.is_alpha;
        info.path = GetCorePath(kc.name);
        info.installed = IsInstalled(kc.name);
        info.version = GetInstalledVersion(kc.name);
        result.push_back(std::move(info));
    }
    return result;
}

InstalledCoreInfo CoreManager::GetCoreInfo(const std::string& name) const {
    InstalledCoreInfo info;
    info.name = name;
    // 查找是否为已知内核以填充 is_alpha
    for (const auto& kc : kKnownCores) {
        if (name == kc.name) {
            info.is_alpha = kc.is_alpha;
            break;
        }
    }
    info.path = GetCorePath(name);
    info.installed = IsInstalled(name);
    info.version = GetInstalledVersion(name);
    return info;
}

fs::path CoreManager::GetCoresJsonPath() const {
    return core::Resources::Get().GetResourcesDir() / "cores.json";
}

nlohmann::json CoreManager::LoadCoresJson() const {
    fs::path p = GetCoresJsonPath();
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return nlohmann::json::object();
    }
    std::ifstream ifs(p);
    if (!ifs.is_open()) {
        return nlohmann::json::object();
    }
    try {
        nlohmann::json j;
        ifs >> j;
        if (!j.is_object()) {
            return nlohmann::json::object();
        }
        return j;
    } catch (...) {
        // 解析失败返回空对象
        return nlohmann::json::object();
    }
}

void CoreManager::SaveCoresJson(const nlohmann::json& j) const {
    fs::path p = GetCoresJsonPath();
    // 确保父目录存在
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
}

std::string CoreManager::GetInstalledVersion(const std::string& name) const {
    nlohmann::json j = LoadCoresJson();
    if (!j.contains(name) || !j[name].is_object()) {
        return {};
    }
    if (!j[name].contains("version") || !j[name]["version"].is_string()) {
        return {};
    }
    return j[name]["version"].get<std::string>();
}

void CoreManager::SaveInstalledVersion(const std::string& name, const std::string& version) {
    nlohmann::json j = LoadCoresJson();
    if (!j.contains(name) || !j[name].is_object()) {
        j[name] = nlohmann::json::object();
    }
    j[name]["version"] = version;
    // 记录安装时间（ISO 8601 格式）
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    j[name]["installed_at"] = std::string(buf);
    SaveCoresJson(j);
}

std::string CoreManager::DetectVersion(const std::string& name) const {
    fs::path core_path = GetCorePath(name);
    std::error_code ec;
    if (core_path.empty() || !fs::exists(core_path, ec)) {
        return {};
    }

#ifdef _WIN32
    // Windows 实现：CreateProcessW 运行 `<core> -v`，通过匿名管道捕获 stdout
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_h = nullptr;
    HANDLE write_h = nullptr;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) {
        return {};
    }
    // 读端由父进程持有，不可被子进程继承
    if (!SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_h);
        CloseHandle(write_h);
        return {};
    }

    // 命令行："<core_path>" -v
    std::wstring cmd = L"\"" + core_path.wstring() + L"\" -v";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nullptr;
    si.hStdOutput = write_h;
    si.hStdError = write_h;

    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW;
    BOOL ok = CreateProcessW(nullptr,
                             cmd_buf.data(),
                             nullptr, nullptr,
                             TRUE,   // 继承句柄
                             flags,
                             nullptr, // 继承父进程环境
                             nullptr, // 继承父进程工作目录
                             &si, &pi);
    // 父进程关闭写端，以便子进程退出后 ReadFile 收到 EOF
    CloseHandle(write_h);
    if (!ok) {
        CloseHandle(read_h);
        return {};
    }
    CloseHandle(pi.hThread);

    // 等待子进程退出，超时 5 秒
    DWORD wait_ret = WaitForSingleObject(pi.hProcess, 5000);
    if (wait_ret == WAIT_TIMEOUT) {
        // 超时：终止进程后返回失败
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        CloseHandle(read_h);
        return {};
    }

    // 读取 stdout 输出（子进程已退出，管道中数据可完整读取）
    std::string output;
    char chunk[4096];
    DWORD got = 0;
    while (ReadFile(read_h, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
        output.append(chunk, got);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(read_h);

    return ExtractVersion(output);
#else
    // POSIX 实现：popen 捕获输出（阻塞，无超时；mihomo -v 通常瞬时返回）
    std::string cmd = "\"" + core_path.string() + "\" -v 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string output;
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        output += chunk;
    }
    pclose(pipe);
    return ExtractVersion(output);
#endif
}

// static
int CoreManager::CompareVersions(const std::string& v1, const std::string& v2) {
    // 移除前导 'v' 或 'V'
    auto normalize = [](const std::string& s) -> std::string {
        std::string out = s;
        if (!out.empty() && (out[0] == 'v' || out[0] == 'V')) {
            out.erase(0, 1);
        }
        return out;
    };

    std::string a = normalize(v1);
    std::string b = normalize(v2);

    // 分割主版本与预发布标签（以 '-' 分隔）
    auto split_pre = [](const std::string& s) -> std::pair<std::string, std::string> {
        size_t pos = s.find('-');
        if (pos == std::string::npos) {
            return {s, ""};
        }
        return {s.substr(0, pos), s.substr(pos + 1)};
    };

    auto [main1, pre1] = split_pre(a);
    auto [main2, pre2] = split_pre(b);

    // 按 '.' 分割主版本号，逐段转 int
    auto split_dot = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, '.')) {
            try {
                parts.push_back(std::stoi(item));
            } catch (...) {
                parts.push_back(0);
            }
        }
        return parts;
    };

    std::vector<int> parts1 = split_dot(main1);
    std::vector<int> parts2 = split_dot(main2);

    // 逐段比较，缺失段视为 0
    size_t n = std::max(parts1.size(), parts2.size());
    for (size_t i = 0; i < n; ++i) {
        int p1 = i < parts1.size() ? parts1[i] : 0;
        int p2 = i < parts2.size() ? parts2[i] : 0;
        if (p1 > p2) return 1;
        if (p1 < p2) return -1;
    }

    // 主版本相同：有预发布标签 < 无标签（alpha 版本 < 稳定版）
    if (pre1.empty() && !pre2.empty()) return 1;
    if (!pre1.empty() && pre2.empty()) return -1;
    if (!pre1.empty() && !pre2.empty()) {
        // 都有预发布标签：按字符串比较
        if (pre1 > pre2) return 1;
        if (pre1 < pre2) return -1;
    }
    return 0;
}

} // namespace clash_yuka::clash
