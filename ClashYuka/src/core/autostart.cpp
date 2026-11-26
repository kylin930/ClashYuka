// ClashYuka 开机自启管理实现
#include "core/autostart.h"

#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/utils.h>

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif

#include <iostream>

namespace clash_yuka::core {

namespace {

// 注册表 Run 项路径与值名
#ifdef __WXMSW__
const wchar_t* kRunKeyPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValueName = L"ClashYuka";
#endif

} // namespace

AutoStart& AutoStart::Get() {
    static AutoStart instance;
    return instance;
}

std::string AutoStart::GetAppPath() const {
    // 使用 wxStandardPaths 获取当前可执行文件完整路径。
    // Windows 下返回 "<install_dir>\ClashYuka.exe"。
    return std::string(wxStandardPaths::Get().GetExecutablePath().ToUTF8());
}

bool AutoStart::Enable() {
#ifdef __WXMSW__
    std::string app_path = GetAppPath();
    if (app_path.empty()) {
        std::cerr << "[AutoStart] GetAppPath() returned empty" << std::endl;
        return false;
    }

    wxRegKey key(wxRegKey::HKCU, kRunKeyPath);
    if (!key.Open(wxRegKey::Write)) {
        // 键不存在时尝试创建（正常情况下 Run 键始终存在）
        if (!key.Create()) {
            std::cerr << "[AutoStart] Failed to open/create Run registry key" << std::endl;
            return false;
        }
    }

    // 值为可执行文件完整路径，用引号包裹以处理路径含空格的情况
    wxString value = wxString::FromUTF8("\"" + app_path + "\"");
    if (!key.SetValue(kRunValueName, value)) {
        std::cerr << "[AutoStart] Failed to set Run registry value" << std::endl;
        return false;
    }
    return true;
#else
    // 非 Windows 平台暂不支持
    return false;
#endif
}

bool AutoStart::Disable() {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU, kRunKeyPath);
    if (!key.Open(wxRegKey::Read)) {
        // 键不存在，视为已禁用
        return true;
    }
    if (!key.HasValue(kRunValueName)) {
        return true;
    }
    if (!key.DeleteValue(kRunValueName)) {
        std::cerr << "[AutoStart] Failed to delete Run registry value" << std::endl;
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AutoStart::IsEnabled() const {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU, kRunKeyPath);
    if (!key.Open(wxRegKey::Read)) {
        return false;
    }
    if (!key.HasValue(kRunValueName)) {
        return false;
    }
    // 校验值是否与当前可执行文件路径匹配
    wxString value;
    if (!key.QueryValue(kRunValueName, value)) {
        return false;
    }
    std::string app_path = GetAppPath();
    if (app_path.empty()) {
        return true;  // 无法获取当前路径时仅检查值是否存在
    }
    // 注册表中的值可能含引号，去除后比较
    wxString expected = wxString::FromUTF8(app_path);
    wxString normalized = value;
    normalized.Replace("\"", "", true);
    return normalized.CmpNoCase(expected) == 0;
#else
    return false;
#endif
}

} // namespace clash_yuka::core
