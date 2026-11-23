#include <doctest/doctest.h>

#ifdef _WIN32

#include <windows.h>

#include <string>

#include "clashyuka/platform/system_proxy_windows.h"

using namespace clashyuka::platform;

namespace {

constexpr wchar_t kTestSubKey[] = L"Software\\ClashYukaTests\\Internet Settings";

void DeleteTestKey() {
    ::RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\ClashYukaTests");
}

void SeedOriginalSettings() {
    HKEY key = nullptr;
    REQUIRE(::RegCreateKeyExW(HKEY_CURRENT_USER, kTestSubKey, 0, nullptr, 0, KEY_WRITE,
                              nullptr, &key, nullptr) == ERROR_SUCCESS);
    DWORD enable = 0;
    const wchar_t* server = L"orig.proxy:1234";
    const wchar_t* bypass = L"orig-bypass";
    const wchar_t* pac = L"http://pac.example/proxy.pac";
    ::RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&enable), sizeof(enable));
    ::RegSetValueExW(key, L"ProxyServer", 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(server),
                     static_cast<DWORD>((wcslen(server) + 1) * sizeof(wchar_t)));
    ::RegSetValueExW(key, L"ProxyOverride", 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(bypass),
                     static_cast<DWORD>((wcslen(bypass) + 1) * sizeof(wchar_t)));
    ::RegSetValueExW(key, L"AutoConfigURL", 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(pac),
                     static_cast<DWORD>((wcslen(pac) + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
}

std::wstring ReadValue(const wchar_t* name) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kTestSubKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buffer[512]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    std::wstring value;
    if (::RegQueryValueExW(key, name, nullptr, &type,
                           reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        value.assign(buffer, size / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0') {
            value.pop_back();
        }
    }
    ::RegCloseKey(key);
    return value;
}

}  // namespace

TEST_CASE("Windows system proxy builds a mixed-port proxy string") {
    CHECK(BuildWindowsProxyServer("127.0.0.1", 7897) ==
          "http=127.0.0.1:7897;https=127.0.0.1:7897");
}

TEST_CASE("Windows system proxy applies and restores previous OS settings") {
    DeleteTestKey();
    SeedOriginalSettings();

    auto controller = CreateWindowsSystemProxyController(kTestSubKey);
    auto before = controller->Query();
    REQUIRE(before.ok());
    CHECK_FALSE(before.value().enabled);
    CHECK(before.value().server == "orig.proxy:1234");

    auto enabled = controller->Enable("127.0.0.1", 7897,
                                      DefaultSystemProxyBypass());
    REQUIRE(enabled.ok());
    CHECK(controller->Active());

    auto applied = controller->Query();
    REQUIRE(applied.ok());
    CHECK(applied.value().enabled);
    CHECK(applied.value().server == "http=127.0.0.1:7897;https=127.0.0.1:7897");
    CHECK(ReadValue(L"AutoConfigURL").empty());

    auto disabled = controller->Disable();
    REQUIRE(disabled.ok());
    CHECK_FALSE(controller->Active());

    auto restored = controller->Query();
    REQUIRE(restored.ok());
    CHECK_FALSE(restored.value().enabled);
    CHECK(restored.value().server == "orig.proxy:1234");
    CHECK(restored.value().bypass == "orig-bypass");
    CHECK(ReadValue(L"AutoConfigURL") == L"http://pac.example/proxy.pac");

    DeleteTestKey();
}

TEST_CASE("Windows system proxy restores settings when the controller is destroyed") {
    DeleteTestKey();
    SeedOriginalSettings();
    {
        auto controller = CreateWindowsSystemProxyController(kTestSubKey);
        REQUIRE(controller->Enable("127.0.0.1", 17897, "localhost").ok());
    }
    CHECK(ReadValue(L"ProxyServer") == L"orig.proxy:1234");
    DeleteTestKey();
}

#endif  // _WIN32
