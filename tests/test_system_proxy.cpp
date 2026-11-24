#include <doctest/doctest.h>

#ifdef _WIN32

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "clashyuka/platform/system_proxy_windows.h"

using namespace clashyuka::platform;

namespace {

constexpr wchar_t kOriginalServer[] = L"orig.proxy:1234";
constexpr wchar_t kOriginalBypass[] = L"orig-bypass";
constexpr wchar_t kPac[] = L"http://pac.example/proxy.pac";
constexpr wchar_t kAppliedServer[] = L"http=127.0.0.1:7897;https=127.0.0.1:7897";

struct TestSettings {
    static std::wstring UniqueName() {
        static unsigned sequence = 0;
        return L"ClashYukaProxyTest-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
               std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++sequence);
    }

    const std::wstring name = UniqueName();
    const std::wstring subKey = L"Software\\" + name;
    const std::filesystem::path journal = std::filesystem::temp_directory_path() / (name + L".json");
    HKEY key = nullptr;

    TestSettings() {
        REQUIRE(::RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                                 nullptr, &key, nullptr) == ERROR_SUCCESS);
    }
    ~TestSettings() {
        ::RegCloseKey(key);
        ::RegDeleteKeyW(HKEY_CURRENT_USER, subKey.c_str());
        std::error_code ec;
        std::filesystem::remove(journal, ec);
        std::filesystem::remove(journal.wstring() + L".tmp", ec);
    }

    void SetRaw(const wchar_t* field, DWORD type, const void* data, DWORD size) const {
        REQUIRE(::RegSetValueExW(key, field, 0, type, static_cast<const BYTE*>(data), size) == ERROR_SUCCESS);
    }
    void Set(const wchar_t* field, const std::wstring& value) const {
        SetRaw(field, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    }
    void SetEnable(DWORD enabled) const { SetRaw(L"ProxyEnable", REG_DWORD, &enabled, sizeof(enabled)); }
    void SetOriginal(DWORD enabled = 0) const {
        SetEnable(enabled);
        Set(L"ProxyServer", kOriginalServer);
        Set(L"ProxyOverride", kOriginalBypass);
        Set(L"AutoConfigURL", kPac);
    }
    void SetApplied() const {
        SetEnable(1);
        Set(L"ProxyServer", kAppliedServer);
        Set(L"ProxyOverride", L"localhost");
        REQUIRE(::RegDeleteValueW(key, L"AutoConfigURL") == ERROR_SUCCESS);
    }
    bool Missing(const wchar_t* field) const {
        return ::RegQueryValueExW(key, field, nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND;
    }
    std::optional<std::wstring> String(const wchar_t* field) const {
        if (Missing(field)) return std::nullopt;
        wchar_t buffer[512]{};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        REQUIRE(::RegQueryValueExW(key, field, nullptr, &type,
                                  reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS);
        REQUIRE(type == REG_SZ);
        REQUIRE(size >= sizeof(wchar_t));
        return std::wstring(buffer, size / sizeof(wchar_t) - 1);
    }
    DWORD EnableValue() const {
        DWORD value = 0;
        DWORD size = sizeof(value);
        DWORD type = 0;
        REQUIRE(::RegQueryValueExW(key, L"ProxyEnable", nullptr, &type,
                                  reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS);
        REQUIRE(type == REG_DWORD);
        REQUIRE(size == sizeof(value));
        return value;
    }
    void CheckOriginal(DWORD enabled = 0) const {
        CHECK(EnableValue() == enabled);
        CHECK(String(L"ProxyServer") == kOriginalServer);
        CHECK(String(L"ProxyOverride") == kOriginalBypass);
        CHECK(String(L"AutoConfigURL") == kPac);
    }
    std::string ReadJournal() const {
        std::ifstream in(journal, std::ios::binary);
        REQUIRE(in.good());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    void WriteJournal(const std::string& text) const {
        std::ofstream out(journal, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        REQUIRE(out.good());
    }
    std::unique_ptr<SystemProxyController> Controller() const {
        return CreateWindowsSystemProxyController(subKey, journal.wstring());
    }
    std::string CaptureJournal() const {
        auto controller = Controller();
        REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
        return ReadJournal();
    }
};

}  // namespace

TEST_CASE("Windows system proxy builds a mixed-port proxy string") {
    CHECK(BuildWindowsProxyServer("127.0.0.1", 7897) ==
          "http=127.0.0.1:7897;https=127.0.0.1:7897");
}

TEST_CASE("Windows system proxy restores an enabled or disabled proxy and PAC") {
    TestSettings settings;
    DWORD prior = 0;
    SUBCASE("enabled") { prior = 1; }
    SUBCASE("disabled") { prior = 0; }
    settings.SetOriginal(prior);
    {
        auto controller = settings.Controller();
        REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
        CHECK(controller->Active());
        CHECK(settings.EnableValue() == 1);
        CHECK(settings.String(L"ProxyServer") == kAppliedServer);
        CHECK(settings.Missing(L"AutoConfigURL"));
    }
    settings.CheckOriginal(prior);
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

TEST_CASE("Windows system proxy restores absent values and distinguishes present empty values") {
    TestSettings settings;
    settings.Set(L"AutoConfigURL", L"");
    auto controller = settings.Controller();
    REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK(settings.Missing(L"AutoConfigURL"));
    REQUIRE(controller->Disable().ok());
    CHECK(settings.Missing(L"ProxyEnable"));
    CHECK(settings.Missing(L"ProxyServer"));
    CHECK(settings.Missing(L"ProxyOverride"));
    CHECK(settings.String(L"AutoConfigURL") == L"");
}

TEST_CASE("Windows system proxy refuses unreadable registry values before takeover") {
    TestSettings settings;
    settings.SetOriginal();
    SUBCASE("wrong registry type") { settings.Set(L"ProxyEnable", L"0"); }
    SUBCASE("short DWORD") {
        const BYTE value = 0;
        settings.SetRaw(L"ProxyEnable", REG_DWORD, &value, sizeof(value));
    }
    SUBCASE("malformed PAC string") {
        const wchar_t value = L'x';
        settings.SetRaw(L"AutoConfigURL", REG_SZ, &value, sizeof(value));
    }
    auto controller = settings.Controller();
    CHECK_FALSE(controller->Query().ok());
    CHECK_FALSE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK_FALSE(controller->Active());
    CHECK(settings.String(L"ProxyServer") == kOriginalServer);
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

TEST_CASE("Windows system proxy rolls back partial takeover without leaving an active proxy") {
    TestSettings settings;
    settings.SetOriginal();
    auto controller = CreateWindowsSystemProxyControllerForTest(settings.subKey, settings.journal.wstring(),
        [&](bool restoring, const wchar_t* field) {
            CHECK(std::filesystem::exists(settings.journal));
            if (!restoring && std::wstring(field) == L"ProxyEnable") {
                CHECK(settings.String(L"ProxyServer") == kAppliedServer);
                CHECK(settings.Missing(L"AutoConfigURL"));
                return false;
            }
            return true;
        });
    REQUIRE(controller);
    CHECK_FALSE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK_FALSE(controller->Active());
    settings.CheckOriginal();
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

TEST_CASE("Windows system proxy resumes an interrupted takeover rollback after restarting") {
    TestSettings settings;
    settings.SetOriginal();
    std::string journal;
    auto failed = CreateWindowsSystemProxyControllerForTest(settings.subKey, settings.journal.wstring(),
        [&](bool restoring, const wchar_t* field) {
            if (!restoring && journal.empty()) journal = settings.ReadJournal();
            return std::wstring(field) != (restoring ? L"ProxyServer" : L"ProxyEnable");
        });
    REQUIRE(failed);
    CHECK_FALSE(failed->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK_FALSE(failed->Active());
    CHECK(settings.ReadJournal() == journal);
    CHECK(settings.EnableValue() == 0);
    CHECK(settings.String(L"ProxyServer") == kAppliedServer);
    CHECK(settings.String(L"ProxyOverride") == kOriginalBypass);
    CHECK(settings.Missing(L"AutoConfigURL"));
    failed.reset();
    auto reopened = settings.Controller();
    CHECK(reopened->Query().ok());
    settings.CheckOriginal();
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

TEST_CASE("Windows system proxy malformed journal blocks takeover and is retained") {
    TestSettings settings;
    settings.SetOriginal();
    auto journal = settings.CaptureJournal();
    settings.SetApplied();
    SUBCASE("truncated JSON") { journal = "{\"original\":"; }
    SUBCASE("out of range DWORD") {
        const std::string original = "\"enable\":0";
        const auto pos = journal.find(original);
        REQUIRE(pos != std::string::npos);
        journal.replace(pos, original.size(), "\"enable\":4294967296");
    }
    settings.WriteJournal(journal);
    auto controller = settings.Controller();
    CHECK_FALSE(controller->Query().ok());
    CHECK_FALSE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK_FALSE(controller->Disable().ok());
    CHECK(settings.ReadJournal() == journal);
    CHECK(settings.EnableValue() == 1);
    CHECK(settings.String(L"ProxyServer") == kAppliedServer);
    CHECK(settings.Missing(L"AutoConfigURL"));
}

TEST_CASE("Windows system proxy preserves external edits and restores independent fields") {
    TestSettings settings;
    settings.SetOriginal();
    auto controller = settings.Controller();
    REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    const auto journal = settings.ReadJournal();
    DWORD enable = 1;
    std::wstring server = kAppliedServer;
    std::wstring bypass = kOriginalBypass;
    std::optional<std::wstring> pac;
    SUBCASE("server") { settings.Set(L"ProxyServer", server = L"external.proxy"); }
    SUBCASE("enable") { settings.SetEnable(enable = 2); }
    SUBCASE("bypass") {
        settings.Set(L"ProxyOverride", bypass = L"external-bypass");
        enable = 0;
        server = kOriginalServer;
        pac = kPac;
    }
    SUBCASE("PAC presence even with empty data") {
        pac = L"";
        settings.Set(L"AutoConfigURL", *pac);
    }
    CHECK_FALSE(controller->Disable().ok());
    controller.reset();
    auto reopened = settings.Controller();
    CHECK_FALSE(reopened->Query().ok());
    CHECK_FALSE(reopened->Enable("127.0.0.1", 7898, "localhost").ok());
    CHECK(settings.ReadJournal() == journal);
    CHECK(settings.EnableValue() == enable);
    CHECK(settings.String(L"ProxyServer") == server);
    CHECK(settings.String(L"ProxyOverride") == bypass);
    CHECK(settings.String(L"AutoConfigURL") == pac);
}

TEST_CASE("Windows system proxy lease prevents another instance from restoring or taking over") {
    TestSettings settings;
    settings.SetOriginal();
    auto owner = settings.Controller();
    auto early = settings.Controller();
    REQUIRE(owner->Enable("127.0.0.1", 7897, "localhost").ok());
    const auto journal = settings.ReadJournal();
    auto late = settings.Controller();
    const auto otherJournal = settings.journal.wstring() + L".tmp";
    auto differentJournal = CreateWindowsSystemProxyController(settings.subKey, otherJournal);
    for (auto* other : {early.get(), late.get(), differentJournal.get()}) {
        CHECK_FALSE(other->Disable().ok());
        CHECK_FALSE(other->Enable("127.0.0.1", 7898, "localhost").ok());
    }
    late.reset();
    CHECK(settings.String(L"ProxyServer") == kAppliedServer);
    CHECK(settings.ReadJournal() == journal);
    REQUIRE(owner->Disable().ok());
    REQUIRE(early->Enable("127.0.0.1", 7898, "localhost").ok());
    REQUIRE(early->Disable().ok());
    settings.CheckOriginal();
}

TEST_CASE("Windows system proxy startup recovery is idempotent") {
    TestSettings settings;
    settings.SetOriginal();
    const auto journal = settings.CaptureJournal();
    CHECK_FALSE(std::filesystem::exists(settings.journal));
    SUBCASE("replay a crash with registry mutations still applied") { settings.SetApplied(); }
    SUBCASE("replay a crash during takeover before the enable flag was written") {
        settings.SetApplied();
        settings.SetEnable(0);
    }
    SUBCASE("replay a crash after restore but before journal removal") {}
    settings.WriteJournal(journal);
    auto controller = settings.Controller();
    CHECK(controller->Query().ok());
    CHECK_FALSE(controller->Active());
    settings.CheckOriginal();
    CHECK_FALSE(std::filesystem::exists(settings.journal));
    // A repeated recovery with original values already present must not write them again.
    settings.WriteJournal(journal);
    auto repeated = CreateWindowsSystemProxyControllerForTest(settings.subKey, settings.journal.wstring(),
        [](bool, const wchar_t*) { CHECK_MESSAGE(false, "idempotent recovery attempted a write"); return false; });
    REQUIRE(repeated);
    REQUIRE(repeated->Disable().ok());
    CHECK_FALSE(std::filesystem::exists(settings.journal));
    settings.CheckOriginal();
}

TEST_CASE("Windows system proxy keeps endpoint conflicts pending over repeated retries") {
    TestSettings settings;
    settings.SetOriginal(2);
    auto controller = settings.Controller();
    REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    const auto journal = settings.ReadJournal();
    settings.Set(L"ProxyServer", L"external.proxy");
    CHECK_FALSE(controller->Disable().ok());
    CHECK(settings.String(L"ProxyOverride") == kOriginalBypass);
    controller.reset();
    auto retry = settings.Controller();
    for (int i = 0; i < 2; ++i) {
        const auto blocked = retry->Disable();
        REQUIRE_FALSE(blocked.ok());
        CHECK(blocked.error().message.find("changed externally") != std::string::npos);
        CHECK(settings.ReadJournal() == journal);
        CHECK(settings.String(L"ProxyServer") == L"external.proxy");
        CHECK(settings.String(L"ProxyOverride") == kOriginalBypass);
        CHECK(settings.EnableValue() == 1);
        CHECK(settings.Missing(L"AutoConfigURL"));
    }
    settings.Set(L"ProxyServer", kOriginalServer);
    REQUIRE(retry->Disable().ok());
    settings.CheckOriginal(2);
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

TEST_CASE("Windows system proxy rechecks endpoint dependencies immediately before restoring") {
    TestSettings settings;
    settings.SetOriginal(2);
    auto controller = CreateWindowsSystemProxyControllerForTest(settings.subKey, settings.journal.wstring(),
        [&](bool restoring, const wchar_t* field) {
            if (restoring && std::wstring(field) == L"ProxyEnable") {
                settings.Set(L"ProxyServer", L"external.proxy");
            }
            return true;
        });
    REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    const auto journal = settings.ReadJournal();
    CHECK_FALSE(controller->Disable().ok());
    CHECK(settings.ReadJournal() == journal);
    CHECK(settings.EnableValue() == 1);
    CHECK(settings.String(L"ProxyServer") == L"external.proxy");
    CHECK(settings.String(L"AutoConfigURL") == kPac);
}

TEST_CASE("Windows system proxy leaves malformed values untouched while restoring independent fields") {
    TestSettings settings;
    settings.SetOriginal();
    auto controller = settings.Controller();
    REQUIRE(controller->Enable("127.0.0.1", 7897, "localhost").ok());
    const auto journal = settings.ReadJournal();
    settings.Set(L"ProxyEnable", L"external");
    const auto result = controller->Disable();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("could not read") != std::string::npos);
    CHECK(settings.String(L"ProxyEnable") == L"external");
    CHECK(settings.String(L"ProxyServer") == kAppliedServer);
    CHECK(settings.String(L"ProxyOverride") == kOriginalBypass);
    CHECK(settings.Missing(L"AutoConfigURL"));
    CHECK(settings.ReadJournal() == journal);
}

TEST_CASE("Windows system proxy failed restore keeps the journal and can resume after reopening") {
    TestSettings settings;
    settings.SetOriginal();
    const auto journal = settings.CaptureJournal();
    settings.SetApplied();
    settings.WriteJournal(journal);
    auto failed = CreateWindowsSystemProxyControllerForTest(settings.subKey, settings.journal.wstring(),
        [](bool restoring, const wchar_t* field) { return !restoring || std::wstring(field) != L"ProxyOverride"; });
    REQUIRE(failed);
    const auto query = failed->Query();
    REQUIRE_FALSE(query.ok());
    CHECK(query.error().message.find("could not restore") != std::string::npos);
    CHECK_FALSE(failed->Enable("127.0.0.1", 7897, "localhost").ok());
    CHECK(settings.ReadJournal() == journal);
    CHECK(settings.String(L"ProxyServer") == kOriginalServer);
    CHECK(settings.String(L"ProxyOverride") == L"localhost");
    failed.reset();
    auto reopened = settings.Controller();
    CHECK(reopened->Query().ok());
    settings.CheckOriginal();
    CHECK_FALSE(std::filesystem::exists(settings.journal));
}

#endif  // _WIN32
