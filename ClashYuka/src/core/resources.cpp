// ClashYuka 资源管理实现（Phase 11 Task 29）
#include "core/resources.h"

#include <system_error>

#include "core/utils/dirs.h"

#ifdef _WIN32
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#else
#  include <unistd.h>
#endif

namespace clash_yuka::core {

namespace fs = std::filesystem;

namespace {

// 获取当前可执行文件所在目录（与 utils/dirs.cpp 中 exe_dir 同实现，
// 此处重复一份因 dirs.cpp 的 exe_dir 为文件内静态函数未导出）。
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

// 当前工作目录（出错返回空路径）
fs::path cwd() {
    std::error_code ec;
    return fs::current_path(ec);
}

// 平台相关的核心可执行文件名后缀（Windows 带 .exe）
#ifdef _WIN32
inline constexpr const char* kExeSuffix = ".exe";
#else
inline constexpr const char* kExeSuffix = "";
#endif

// 已知 clash 核心清单：{显示名, 文件名(不含后缀), 是否 alpha}
struct KnownCore {
    const char* name;
    const char* base;  // 文件名（不含平台后缀）
    bool is_alpha;
};

const KnownCore kKnownCores[] = {
    {"mihomo",        "mihomo",        false},
    {"mihomo-alpha",  "mihomo-alpha",  true},
    {"clash-rs",      "clash-rs",      false},
    {"clash-rs-alpha","clash-rs-alpha", true},
};

// 拼接平台可执行文件名：base + kExeSuffix
std::string platform_exe(const char* base) {
    std::string s(base);
    s += kExeSuffix;
    return s;
}

} // namespace

Resources& Resources::Get() {
    static Resources instance;
    return instance;
}

fs::path Resources::ResolveResourcesDir() {
    const auto prod = utils::app_resources_dir();  // <exe_dir>/resources

    // 候选目录列表，按优先级排序
    std::vector<fs::path> candidates;
    candidates.push_back(prod);

    // macOS .app bundle：可执行文件位于 Contents/MacOS/，资源在 Contents/Resources/
#ifdef __APPLE__
    candidates.push_back(exe_dir() / ".." / "Resources");
#endif

    // 开发环境回退：相对可执行文件向上若干级查找 resources/ 目录
    // （覆盖 build/Debug、build/Release、build 等常见构建输出位置）
    {
        const fs::path exe = exe_dir();
        candidates.push_back(exe / ".." / "resources");
        candidates.push_back(exe / ".." / ".." / "resources");
        candidates.push_back(exe / ".." / ".." / ".." / "resources");
    }

    // 当前工作目录下的 resources/（便于从源码根目录直接运行）
    {
        const fs::path c = cwd();
        candidates.push_back(c / "resources");
        candidates.push_back(c / ".." / "resources");
    }

    // 返回首个存在且为目录的候选；全部不存在则回退到正式部署路径
    std::error_code ec;
    for (const auto& cand : candidates) {
        if (fs::exists(cand, ec) && fs::is_directory(cand, ec)) {
            // 弱规范化（不抛异常）
            return fs::weakly_canonical(cand, ec);
        }
    }
    return prod;
}

Resources::Resources()
    : resources_dir_(ResolveResourcesDir()) {}

const fs::path& Resources::GetResourcesDir() const {
    return resources_dir_;
}

fs::path Resources::GetMihomoPath() const {
    return resources_dir_ / platform_exe("mihomo");
}

fs::path Resources::GetMihomoAlphaPath() const {
    return resources_dir_ / platform_exe("mihomo-alpha");
}

fs::path Resources::GetClashRsPath() const {
    return resources_dir_ / platform_exe("clash-rs");
}

fs::path Resources::GetServiceHelperPath() const {
    return resources_dir_ / platform_exe("clash-yuka-service");
}

fs::path Resources::GetGeoIPPath() const {
    return resources_dir_ / "Country.mmdb";
}

fs::path Resources::GetGeoSitePath() const {
    return resources_dir_ / "geosite.dat";
}

std::vector<Resources::CoreEntry> Resources::ListAvailableCores() const {
    std::vector<CoreEntry> result;
    result.reserve(sizeof(kKnownCores) / sizeof(kKnownCores[0]));

    std::error_code ec;
    for (const auto& core : kKnownCores) {
        fs::path p = resources_dir_ / platform_exe(core.base);
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
            CoreEntry e;
            e.name = core.name;
            e.path = std::move(p);
            e.is_alpha = core.is_alpha;
            result.push_back(std::move(e));
        }
    }
    return result;
}

void Resources::EnsureResourcesDir() const {
    std::error_code ec;
    fs::create_directories(resources_dir_, ec);
    // 失败时静默忽略：调用方在访问具体资源文件时再处理不存在的情况
}

} // namespace clash_yuka::core
