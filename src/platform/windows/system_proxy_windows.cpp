#include "clashyuka/platform/system_proxy.h"

#include <windows.h>
#include <wininet.h>

#include <string>
#include <vector>

#include "clashyuka/platform/system_proxy_windows.h"

namespace clashyuka::platform {
namespace {

using core::ErrorKind;
using core::MakeError;

constexpr wchar_t kDefaultSubKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          wide.data(), length);
    return wide;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0,
                                             nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          utf8.data(), length, nullptr, nullptr);
    return utf8;
}

struct KeyHandle {
    HKEY key = nullptr;
    ~KeyHandle() {
        if (key != nullptr) {
            ::RegCloseKey(key);
        }
    }
};

bool OpenSettings(const std::wstring& subKey, KeyHandle& handle) {
    const LONG status = ::RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                                          KEY_READ | KEY_WRITE, nullptr, &handle.key, nullptr);
    return status == ERROR_SUCCESS;
}

struct Snapshot {
    bool valid = false;
    bool hadEnable = false;
    DWORD enable = 0;
    bool hadServer = false;
    std::wstring server;
    bool hadOverride = false;
    std::wstring overrideList;
    bool hadAutoConfig = false;
    std::wstring autoConfigUrl;
};

std::wstring ReadString(HKEY key, const wchar_t* name, bool& present) {
    present = false;
    DWORD type = 0;
    DWORD size = 0;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size == 0) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (::RegQueryValueExW(key, name, nullptr, &type,
                           reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    present = true;
    return value;
}

Snapshot ReadSnapshot(HKEY key) {
    Snapshot snapshot;
    snapshot.valid = true;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    DWORD enable = 0;
    if (::RegQueryValueExW(key, L"ProxyEnable", nullptr, &type,
                           reinterpret_cast<LPBYTE>(&enable), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        snapshot.hadEnable = true;
        snapshot.enable = enable;
    }
    snapshot.server = ReadString(key, L"ProxyServer", snapshot.hadServer);
    snapshot.overrideList = ReadString(key, L"ProxyOverride", snapshot.hadOverride);
    snapshot.autoConfigUrl = ReadString(key, L"AutoConfigURL", snapshot.hadAutoConfig);
    return snapshot;
}

bool RemoveValue(HKEY key, const wchar_t* name) {
    const LONG status = ::RegDeleteValueW(key, name);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value, bool present) {
    if (!present) {
        return RemoveValue(key, name);
    }
    return ::RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
}

bool Restore(HKEY key, const Snapshot& snapshot) {
    const bool server = WriteString(key, L"ProxyServer", snapshot.server, snapshot.hadServer);
    const bool bypass = WriteString(key, L"ProxyOverride", snapshot.overrideList, snapshot.hadOverride);
    const bool pac = WriteString(key, L"AutoConfigURL", snapshot.autoConfigUrl, snapshot.hadAutoConfig);
    const bool enable = snapshot.hadEnable
        ? ::RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&snapshot.enable), sizeof(DWORD)) == ERROR_SUCCESS
        : RemoveValue(key, L"ProxyEnable");
    return server && bypass && pac && enable;
}

void NotifySettingsChanged() {
    ::InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    ::InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

class WindowsSystemProxyController final : public SystemProxyController {
public:
    explicit WindowsSystemProxyController(std::wstring subKey)
        : subKey_(subKey.empty() ? kDefaultSubKey : std::move(subKey)) {}

    ~WindowsSystemProxyController() override { Disable(); }

    core::Result<void> Enable(const std::string& host, int port,
                              const std::string& bypass) override {
        if (host.empty() || port <= 0 || port > 65535) {
            return MakeError(ErrorKind::InvalidConfig, "invalid system proxy endpoint");
        }
        KeyHandle key;
        if (!OpenSettings(subKey_, key)) {
            return MakeError(ErrorKind::Internal, "could not open the system proxy registry key");
        }
        if (!active_) {
            saved_ = ReadSnapshot(key.key);
        }
        const std::wstring server = ToWide(BuildWindowsProxyServer(host, port));
        const DWORD enable = 1;
        server_ = ToUtf8(server);
        active_ = true;
        if (!WriteString(key.key, L"ProxyServer", server, true) ||
            !WriteString(key.key, L"ProxyOverride", ToWide(bypass), true) ||
            !RemoveValue(key.key, L"AutoConfigURL") ||
            ::RegSetValueExW(key.key, L"ProxyEnable", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&enable), sizeof(DWORD)) !=
                ERROR_SUCCESS) {
            if (!Disable()) {
                return MakeError(ErrorKind::Internal, "system proxy write and rollback both failed");
            }
            return MakeError(ErrorKind::Internal, "could not write the system proxy settings");
        }
        if (subKey_ == kDefaultSubKey) {
            NotifySettingsChanged();
        }
        return {};
    }

    core::Result<void> Disable() override {
        if (!active_) {
            return {};
        }
        KeyHandle key;
        if (!OpenSettings(subKey_, key)) {
            return MakeError(ErrorKind::Internal, "could not open the system proxy registry key");
        }
        if (!Restore(key.key, saved_)) {
            return MakeError(ErrorKind::Internal, "could not restore the previous system proxy settings");
        }
        active_ = false;
        server_.clear();
        if (subKey_ == kDefaultSubKey) {
            NotifySettingsChanged();
        }
        return {};
    }

    core::Result<SystemProxyState> Query() override {
        KeyHandle key;
        if (!OpenSettings(subKey_, key)) {
            return MakeError(ErrorKind::Internal, "could not open the system proxy registry key");
        }
        SystemProxyState state;
        DWORD type = 0;
        DWORD size = sizeof(DWORD);
        DWORD enable = 0;
        if (::RegQueryValueExW(key.key, L"ProxyEnable", nullptr, &type,
                               reinterpret_cast<LPBYTE>(&enable), &size) == ERROR_SUCCESS &&
            type == REG_DWORD) {
            state.enabled = enable != 0;
        }
        bool present = false;
        state.server = ToUtf8(ReadString(key.key, L"ProxyServer", present));
        state.bypass = ToUtf8(ReadString(key.key, L"ProxyOverride", present));
        return state;
    }

    bool Active() const override { return active_; }

    std::string ActiveServer() const override { return server_; }

private:
    std::wstring subKey_;
    Snapshot saved_;
    bool active_ = false;
    std::string server_;
};

}  // namespace

std::string BuildWindowsProxyServer(const std::string& host, int port) {
    return "http=" + host + ":" + std::to_string(port) + ";https=" + host + ":" +
           std::to_string(port);
}

std::unique_ptr<SystemProxyController> CreateWindowsSystemProxyController(
    const std::wstring& subKey) {
    return std::make_unique<WindowsSystemProxyController>(subKey);
}

std::unique_ptr<SystemProxyController> CreateSystemProxyController() {
    return CreateWindowsSystemProxyController({});
}

std::string DefaultSystemProxyBypass() {
    return "localhost;127.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;"
           "172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;"
           "172.30.*;172.31.*;192.168.*;<local>";
}

}  // namespace clashyuka::platform
