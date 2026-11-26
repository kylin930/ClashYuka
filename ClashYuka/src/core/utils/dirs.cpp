// ClashYuka 跨平台目录定位实现
#include "dirs.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#else
#  include <unistd.h>
#endif

namespace clash_yuka::utils {

namespace fs = std::filesystem;

namespace {

// 读取环境变量到 std::string（不存在返回空）
std::string getenv_str(const char* key) {
    if (!key) return {};
#if defined(_WIN32) || defined(_MSC_VER)
    // _WIN32 上 getenv 在 /utf-8 下返回 UTF-8
    if (const char* v = std::getenv(key); v && *v) return std::string(v);
    return {};
#else
    if (const char* v = std::getenv(key); v && *v) return std::string(v);
    return {};
#endif
}

// 用户主目录
fs::path home_dir() {
#ifdef _WIN32
    std::string p = getenv_str("USERPROFILE");
    if (!p.empty()) return fs::path(p);
    std::string drive = getenv_str("HOMEDRIVE");
    std::string pathp = getenv_str("HOMEPATH");
    if (!drive.empty() && !pathp.empty()) return fs::path(drive + pathp);
    return fs::current_path();
#else
    std::string p = getenv_str("HOME");
    if (!p.empty()) return fs::path(p);
    return fs::current_path();
#endif
}

// 确保目录存在（递归创建），失败时返回原路径不抛异常
fs::path ensure_dir(fs::path p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

// 获取当前可执行文件所在目录
fs::path exe_dir() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buf).parent_path();
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX] = {0};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        fs::path p(buf);
        if (p.has_root_path()) return p.parent_path();
    }
#else
    if (fs::exists("/proc/self/exe", ec)) {
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) return exe.parent_path();
    }
#endif
    return fs::current_path(ec);
}

} // namespace

fs::path app_config_dir() {
#ifdef _WIN32
    std::string appdata = getenv_str("APPDATA");
    fs::path base = appdata.empty() ? (home_dir() / "AppData" / "Roaming") : fs::path(appdata);
    return ensure_dir(base / kAppNameDisplay);
#elif defined(__APPLE__)
    return ensure_dir(home_dir() / "Library" / "Application Support" / kAppName);
#else
    std::string xdg = getenv_str("XDG_CONFIG_HOME");
    fs::path base = xdg.empty() ? (home_dir() / ".config") : fs::path(xdg);
    return ensure_dir(base / kAppName);
#endif
}

fs::path app_data_dir() {
#ifdef _WIN32
    std::string local = getenv_str("LOCALAPPDATA");
    fs::path base = local.empty() ? (home_dir() / "AppData" / "Local") : fs::path(local);
    return ensure_dir(base / kAppNameDisplay);
#elif defined(__APPLE__)
    return app_config_dir();
#else
    std::string xdg = getenv_str("XDG_DATA_HOME");
    fs::path base = xdg.empty() ? (home_dir() / ".local" / "share") : fs::path(xdg);
    return ensure_dir(base / kAppName);
#endif
}

fs::path app_log_dir() {
    return ensure_dir(app_data_dir() / "logs");
}

fs::path app_resources_dir() {
    return exe_dir() / "resources";
}

fs::path verge_config_path() {
    return app_config_dir() / kVergeConfigFile;
}

fs::path profiles_yaml_path() {
    return app_config_dir() / kProfilesYamlFile;
}

fs::path profiles_dir() {
    return ensure_dir(app_data_dir() / "profiles");
}

fs::path clash_guard_overrides_path() {
    return app_config_dir() / kClashGuardOverridesFile;
}

} // namespace clash_yuka::utils
