// ClashYuka 系统代理管理实现
#include "core/sysopt.h"

#include <wx/string.h>

#ifdef __WXMSW__
#include <wx/msw/registry.h>

#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

#include <chrono>
#include <iostream>

#ifndef _WIN32
#  include <unistd.h>
#endif

namespace clash_yuka::core {

bool IsProcessElevated() {
#ifdef _WIN32
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&nt_authority, 2,
                                   SECURITY_BUILTIN_DOMAIN_RID,
                                   DOMAIN_ALIAS_RID_ADMINS,
                                   0, 0, 0, 0, 0, 0, &admin_group)) {
        ::CheckTokenMembership(nullptr, admin_group, &is_admin);
        ::FreeSid(admin_group);
    }
    return is_admin != FALSE;
#else
    return ::geteuid() == 0;
#endif
}

namespace {

#ifdef __WXMSW__
// Internet Settings 注册表路径
const wchar_t* kInternetSettingsKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
const wchar_t* kProxyEnableName = L"ProxyEnable";
const wchar_t* kProxyServerName = L"ProxyServer";
const wchar_t* kProxyOverrideName = L"ProxyOverride";
#endif

} // namespace

SysProxy& SysProxy::Get() {
    static SysProxy instance;
    return instance;
}

SysProxy::SysProxy() = default;

SysProxy::~SysProxy() {
    StopGuard();
}

bool SysProxy::ReadRegistry(uint32_t& proxy_enable, std::string& proxy_server,
                            std::string& bypass) {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU, kInternetSettingsKey);
    if (!key.Open(wxRegKey::Read)) {
        return false;
    }

    // ProxyEnable（DWORD）
    long enable_val = 0;
    if (key.HasValue(kProxyEnableName) && key.QueryValue(kProxyEnableName, &enable_val)) {
        proxy_enable = static_cast<uint32_t>(enable_val);
    } else {
        proxy_enable = 0;
    }

    // ProxyServer（REG_SZ）
    wxString server;
    if (key.HasValue(kProxyServerName) && key.QueryValue(kProxyServerName, server)) {
        proxy_server = std::string(server.ToUTF8());
    } else {
        proxy_server.clear();
    }

    // ProxyOverride（REG_SZ）
    wxString override;
    if (key.HasValue(kProxyOverrideName) && key.QueryValue(kProxyOverrideName, override)) {
        bypass = std::string(override.ToUTF8());
    } else {
        bypass.clear();
    }
    return true;
#else
    (void)proxy_enable;
    (void)proxy_server;
    (void)bypass;
    return false;
#endif
}

bool SysProxy::WriteRegistry(bool enable, const std::string& server,
                             const std::string& bypass) {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU, kInternetSettingsKey);
    if (!key.Open(wxRegKey::Write)) {
        if (!key.Create()) {
            std::cerr << "[SysProxy] Failed to open Internet Settings key" << std::endl;
            return false;
        }
    }

    if (!key.SetValue(kProxyEnableName, static_cast<long>(enable ? 1 : 0))) {
        std::cerr << "[SysProxy] Failed to set ProxyEnable" << std::endl;
        return false;
    }

    if (enable) {
        if (!key.SetValue(kProxyServerName, wxString::FromUTF8(server))) {
            std::cerr << "[SysProxy] Failed to set ProxyServer" << std::endl;
            return false;
        }
        // bypass 非空时才写入 ProxyOverride
        if (!bypass.empty()) {
            if (!key.SetValue(kProxyOverrideName, wxString::FromUTF8(bypass))) {
                std::cerr << "[SysProxy] Failed to set ProxyOverride" << std::endl;
                return false;
            }
        }
    }
    return true;
#else
    (void)enable;
    (void)server;
    (void)bypass;
    return false;
#endif
}

void SysProxy::NotifySettingsChanged() {
#ifdef __WXMSW__
    // 通知系统代理设置已变更，使新设置立即生效
    InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
#endif
}

void SysProxy::Enable(uint16_t port, const std::string& bypass) {
    port_ = port;
    bypass_ = bypass;
    enabled_ = true;

    std::string server = "127.0.0.1:" + std::to_string(port);
    if (WriteRegistry(true, server, bypass)) {
        NotifySettingsChanged();
    }
}

void SysProxy::Disable() {
    enabled_ = false;
    if (WriteRegistry(false, "", "")) {
        NotifySettingsChanged();
    }
}

void SysProxy::StartGuard(uint16_t port, const std::string& bypass, uint32_t interval_sec) {
    // 若已在运行，先停止旧线程
    StopGuard();

    port_ = port;
    bypass_ = bypass;

    // interval_sec 至少 1 秒，避免空转
    if (interval_sec == 0) interval_sec = 10;

    guard_running_ = true;
    guard_thread_ = std::thread(&SysProxy::GuardLoop, this, interval_sec);
}

void SysProxy::StopGuard() {
    guard_running_ = false;
    if (guard_thread_.joinable()) {
        guard_thread_.join();
    }
}

void SysProxy::GuardLoop(uint32_t interval_sec) {
    std::string expected_server = "127.0.0.1:" + std::to_string(port_);

    while (guard_running_) {
        // 读取当前注册表值，校验是否被外部修改
        uint32_t cur_enable = 0;
        std::string cur_server;
        std::string cur_bypass;
        if (ReadRegistry(cur_enable, cur_server, cur_bypass)) {
            bool need_fix = false;
            if (cur_enable != 1) {
                need_fix = true;
            } else if (cur_server != expected_server) {
                need_fix = true;
            }
            // bypass 不强制校验（部分软件可能追加条目），仅当被清空且原值非空时修复
            if (!bypass_.empty() && cur_bypass.empty()) {
                need_fix = true;
            }

            if (need_fix) {
                std::cerr << "[SysProxy] Guard: proxy settings modified externally, restoring..."
                          << std::endl;
                if (WriteRegistry(true, expected_server, bypass_)) {
                    NotifySettingsChanged();
                }
            }
        }

        // 按间隔休眠，每秒检查一次退出标志以加快响应
        for (uint32_t i = 0; i < interval_sec && guard_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

} // namespace clash_yuka::core
