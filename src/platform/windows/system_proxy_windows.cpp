#include "clashyuka/platform/system_proxy.h"

#include <windows.h>
#include <wininet.h>

#include <string>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <cwctype>
#include <utility>

#include <nlohmann/json.hpp>

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
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                          wide.data(), length);
    return wide;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0,
                                             nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
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

bool ReadDword(HKEY key, const wchar_t* name, bool& present, DWORD& value) {
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG status = ::RegQueryValueExW(key, name, nullptr, &type,
                                           reinterpret_cast<LPBYTE>(&value), &size);
    present = status == ERROR_SUCCESS;
    if (status == ERROR_FILE_NOT_FOUND) {
        value = 0;
        return true;
    }
    return present && type == REG_DWORD && size == sizeof(value);
}

bool ReadStringStrict(HKEY key, const wchar_t* name, bool& present, std::wstring& value) {
    DWORD type = 0;
    DWORD size = 0;
    LONG status = ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS || type != REG_SZ || size == 0 || size % sizeof(wchar_t) != 0) return false;
    value.resize(size / sizeof(wchar_t));
    status = ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size);
    if (status != ERROR_SUCCESS || type != REG_SZ || size == 0 || size % sizeof(wchar_t) != 0) return false;
    value.resize(size / sizeof(wchar_t));
    if (value.find(L'\0') != value.size() - 1) return false;
    value.pop_back();
    if (!value.empty() && ToUtf8(value).empty()) return false;
    present = true;
    return true;
}

enum Field { EnableField, ServerField, OverrideField, PacField };
constexpr const wchar_t* kFieldNames[] = {L"ProxyEnable", L"ProxyServer", L"ProxyOverride", L"AutoConfigURL"};
constexpr Field kFields[] = {EnableField, ServerField, OverrideField, PacField};
constexpr Field kEndpointFields[] = {EnableField, ServerField, PacField};

bool ReadField(HKEY key, Field field, Snapshot& s) {
    const auto name = kFieldNames[field];
    switch (field) {
        case EnableField: return ReadDword(key, name, s.hadEnable, s.enable);
        case ServerField: return ReadStringStrict(key, name, s.hadServer, s.server);
        case OverrideField: return ReadStringStrict(key, name, s.hadOverride, s.overrideList);
        case PacField: return ReadStringStrict(key, name, s.hadAutoConfig, s.autoConfigUrl);
    }
    return false;
}

Snapshot ReadSnapshot(HKEY key) {
    Snapshot snapshot;
    snapshot.valid = true;
    for (const auto field : kFields) snapshot.valid = ReadField(key, field, snapshot) && snapshot.valid;
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

bool SameField(const Snapshot& a, const Snapshot& b, Field field) {
    switch (field) {
        case EnableField: return a.hadEnable == b.hadEnable && a.enable == b.enable;
        case ServerField: return a.hadServer == b.hadServer && a.server == b.server;
        case OverrideField: return a.hadOverride == b.hadOverride && a.overrideList == b.overrideList;
        case PacField: return a.hadAutoConfig == b.hadAutoConfig && a.autoConfigUrl == b.autoConfigUrl;
    }
    return false;
}

bool WriteField(HKEY key, Field field, const Snapshot& target) {
    const auto name = kFieldNames[field];
    switch (field) {
        case EnableField:
            return target.hadEnable
                ? ::RegSetValueExW(key, name, 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&target.enable), sizeof(DWORD)) == ERROR_SUCCESS
                : RemoveValue(key, name);
        case ServerField: return WriteString(key, name, target.server, target.hadServer);
        case OverrideField: return WriteString(key, name, target.overrideList, target.hadOverride);
        case PacField: return WriteString(key, name, target.autoConfigUrl, target.hadAutoConfig);
    }
    return false;
}

using WriteGate = std::function<bool(bool, const wchar_t*)>;

bool ApplyChanges(HKEY key, const Snapshot& original, const Snapshot& applied, const WriteGate& gate) {
    constexpr Field order[] = {ServerField, OverrideField, PacField, EnableField};
    const auto matchesProgress = [&](std::size_t completed) {
        const auto observed = ReadSnapshot(key);
        if (!observed.valid) return false;
        for (std::size_t i = 0; i < std::size(order); ++i) {
            if (!SameField(observed, i < completed ? applied : original, order[i])) return false;
        }
        return true;
    };
    for (std::size_t i = 0; i < std::size(order); ++i) {
        const auto field = order[i];
        if (SameField(original, applied, field)) continue;
        if ((gate && !gate(false, kFieldNames[field])) || !matchesProgress(i) ||
            !WriteField(key, field, applied)) return false;
    }
    return matchesProgress(std::size(order));
}

enum class FieldState { Original, Applied, Conflict, ReadError };

FieldState InspectField(HKEY key, Field field, const Snapshot& original, const Snapshot& applied) {
    Snapshot observed;
    if (!ReadField(key, field, observed)) return FieldState::ReadError;
    if (SameField(observed, original, field)) return FieldState::Original;
    if (SameField(observed, applied, field)) return FieldState::Applied;
    return FieldState::Conflict;
}

struct RestoreResult {
    bool changed = false;
    bool conflict = false;
    bool readError = false;
    bool incomplete = false;

    bool Verified(FieldState state) {
        conflict |= state == FieldState::Conflict;
        readError |= state == FieldState::ReadError;
        return state == FieldState::Original || state == FieldState::Applied;
    }

    const char* Error() const {
        if (readError) return "could not read the current system proxy settings; recovery is pending";
        if (conflict) return "system proxy changed externally; recovery is pending";
        if (incomplete) return "could not restore the system proxy settings; recovery is pending";
        return nullptr;
    }
};

RestoreResult RestoreChanges(HKEY key, const Snapshot& original, const Snapshot& applied, const WriteGate& gate) {
    RestoreResult result;
    const auto inspect = [&](Field field) { return InspectField(key, field, original, applied); };
    const auto restore = [&](Field field) {
        auto state = inspect(field);
        if (!result.Verified(state)) return false;
        if (state == FieldState::Original) return true;
        if (gate && !gate(true, kFieldNames[field])) {
            result.incomplete = true;
            return false;
        }
        // Leave the endpoint group alone when any member is ambiguous. In particular,
        // bringing back ProxyEnable or the PAC must not activate an external endpoint.
        if (field != OverrideField) {
            for (const auto other : kEndpointFields) {
                if (other == field) continue;
                const auto otherState = inspect(other);
                if (!result.Verified(otherState)) return false;
                const bool mustBeOriginal = (field == EnableField && original.enable != 0) ||
                    (other == EnableField && original.enable == 0) ||
                    (field == PacField && other == ServerField);
                if (mustBeOriginal && otherState != FieldState::Original) {
                    result.incomplete = true;
                    return false;
                }
            }
        }
        // Re-read the value being written last, after checking its dependencies.
        state = inspect(field);
        if (!result.Verified(state)) return false;
        if (state == FieldState::Original) return true;
        if (!WriteField(key, field, original)) {
            result.incomplete = true;
            return false;
        }
        result.changed = true;
        return true;
    };

    restore(OverrideField);
    // Turn off the app proxy first if the original was off; turn it back on only
    // after restoring its original endpoint. Stop dependent writes on failure.
    if (original.enable != 0 || restore(EnableField)) {
        if (restore(ServerField) && restore(PacField) && original.enable != 0) restore(EnableField);
    }
    for (const auto field : kFields) {
        const auto state = inspect(field);
        result.Verified(state);
        result.incomplete |= state == FieldState::Applied;
    }
    return result;
}

std::string SnapshotJson(const Snapshot& s) {
    nlohmann::json j = {{"version", 1}, {"hadEnable", s.hadEnable}, {"enable", s.enable},
        {"hadServer", s.hadServer}, {"server", ToUtf8(s.server)},
        {"hadOverride", s.hadOverride}, {"override", ToUtf8(s.overrideList)},
        {"hadAutoConfig", s.hadAutoConfig}, {"autoConfig", ToUtf8(s.autoConfigUrl)}};
    return j.dump();
}

bool ParseSnapshot(const std::string& text, Snapshot& s) {
    try {
        const auto j = nlohmann::json::parse(text);
        if (!j.at("version").is_number_integer() || j.at("version") != 1) return false;
        const auto& enable = j.at("enable");
        if (!enable.is_number_unsigned() || enable.get<unsigned long long>() > (std::numeric_limits<DWORD>::max)()) return false;
        s.hadEnable = j.at("hadEnable").get<bool>(); s.enable = enable.get<DWORD>();
        s.hadServer = j.at("hadServer").get<bool>(); s.server = ToWide(j.at("server").get<std::string>());
        s.hadOverride = j.at("hadOverride").get<bool>(); s.overrideList = ToWide(j.at("override").get<std::string>());
        s.hadAutoConfig = j.at("hadAutoConfig").get<bool>(); s.autoConfigUrl = ToWide(j.at("autoConfig").get<std::string>());
        const auto validString = [](bool present, const std::wstring& value) {
            return (present || value.empty()) && value.find(L'\0') == std::wstring::npos;
        };
        s.valid = (s.hadEnable || s.enable == 0) && validString(s.hadServer, s.server) &&
            validString(s.hadOverride, s.overrideList) && validString(s.hadAutoConfig, s.autoConfigUrl);
        return s.valid;
    } catch (...) { return false; }
}

class OwnershipLease {
public:
    ~OwnershipLease() { Release(); }

    bool Acquire(const std::wstring& name) {
        if (handle_) return true;
        // The named kernel object's lifetime is the lease; unlike a mutex wait,
        // creating it rejects another controller even on the same thread.
        const HANDLE handle = ::CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle) return false;
        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            ::CloseHandle(handle);
            return false;
        }
        handle_ = handle;
        return true;
    }

    void Release() {
        if (handle_) ::CloseHandle(handle_);
        handle_ = nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

std::wstring MutexName(std::wstring subKey) {
    for (auto& c : subKey) c = static_cast<wchar_t>(std::towupper(c));
    return L"Local\\ClashYuka.SystemProxy." + std::to_wstring(std::hash<std::wstring>{}(subKey));
}

bool SaveJournal(const std::wstring& path, const Snapshot& original, const Snapshot& applied) {
    const auto content = nlohmann::json{{"original", nlohmann::json::parse(SnapshotJson(original))},
                                        {"applied", nlohmann::json::parse(SnapshotJson(applied))}}.dump();
    const std::filesystem::path target(path), temp = target.wstring() + L".tmp";
    std::error_code ec;
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return false;
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    if (!out) return false;
    return MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool LoadJournal(const std::wstring& path, Snapshot& original, Snapshot& applied, bool& exists) {
    std::error_code ec;
    exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    if (!exists) return true;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        if (in.bad()) return false;
        const auto j = nlohmann::json::parse(text);
        // Older journals may contain an "owned" snapshot of external edits. Ignore it.
        return ParseSnapshot(j.at("original").dump(), original) &&
               ParseSnapshot(j.at("applied").dump(), applied);
    } catch (...) { return false; }
}

bool RemoveJournal(const std::wstring& path) { std::error_code ec; std::filesystem::remove(path, ec); return !ec; }

bool NotifyAllowed(const std::wstring& subKey) { return subKey == kDefaultSubKey; }

void NotifySettingsChanged() {
    ::InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    ::InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

class WindowsSystemProxyController final : public SystemProxyController {
public:
    explicit WindowsSystemProxyController(std::wstring subKey, std::wstring journalPath,
                                          WriteGate gate = {})
        : subKey_(subKey.empty() ? kDefaultSubKey : std::move(subKey)),
          journalPath_(journalPath.empty() ? DefaultJournalPath() : std::move(journalPath)),
          mutexName_(MutexName(subKey_)), gate_(std::move(gate)) {
        Disable();
    }

    ~WindowsSystemProxyController() override { if (active_) Disable(); }

    core::Result<void> Enable(const std::string& host, int port,
                              const std::string& bypass) override {
        if (host.empty() || port <= 0 || port > 65535) {
            return MakeError(ErrorKind::InvalidConfig, "invalid system proxy endpoint");
        }
        const auto endpoint = BuildWindowsProxyServer(host, port);
        Snapshot applied;
        applied.valid = true;
        applied.hadEnable = true;
        applied.enable = 1;
        applied.hadServer = true;
        applied.server = ToWide(endpoint);
        applied.hadOverride = true;
        applied.overrideList = ToWide(bypass);
        if (applied.server.empty() || (!bypass.empty() && applied.overrideList.empty()) ||
            applied.server.find(L'\0') != std::wstring::npos ||
            applied.overrideList.find(L'\0') != std::wstring::npos) {
            return MakeError(ErrorKind::InvalidConfig, "invalid system proxy settings");
        }
        if (active_) {
            auto stopped = Disable();
            if (!stopped) return stopped;
        }
        if (!lease_.Acquire(mutexName_)) return MakeError(ErrorKind::Internal, "system proxy operation is busy");
        if (recoveryError_) return Fail("system proxy recovery requires manual retry");

        Snapshot pendingOriginal, pendingApplied;
        bool exists = false;
        if (!LoadJournal(journalPath_, pendingOriginal, pendingApplied, exists)) {
            return Fail("could not read the system proxy recovery record", true);
        }
        if (exists) return Fail("system proxy recovery is pending", true);
        KeyHandle key;
        if (!OpenSettings(subKey_, key)) return Fail("could not open the system proxy registry key");
        const auto original = ReadSnapshot(key.key);
        if (!original.valid) return Fail("could not read the previous system proxy settings");
        if (!SaveJournal(journalPath_, original, applied)) return Fail("could not save system proxy recovery data");

        if (!ApplyChanges(key.key, original, applied, gate_)) {
            auto recovery = Restore(key.key, original, applied);
            if (!recovery) return recovery;
            return Fail("could not write the system proxy settings");
        }
        server_ = endpoint;
        active_ = true;
        if (NotifyAllowed(subKey_)) NotifySettingsChanged();
        return {};
    }

    core::Result<void> Disable() override {
        if (!lease_.Acquire(mutexName_)) return MakeError(ErrorKind::Internal, "system proxy operation is busy");
        Snapshot original, applied;
        bool exists = false;
        if (!LoadJournal(journalPath_, original, applied, exists)) {
            return Fail("could not read the system proxy recovery record; settings were left unchanged", true);
        }
        if (!exists && active_) return Fail("system proxy recovery record is missing", true);
        if (exists) {
            KeyHandle key;
            if (!OpenSettings(subKey_, key)) return Fail("could not open the system proxy registry key", true);
            auto recovery = Restore(key.key, original, applied);
            if (!recovery) return recovery;
        }
        active_ = false;
        server_.clear();
        recoveryError_ = nullptr;
        lease_.Release();
        return {};
    }

    core::Result<SystemProxyState> Query() override {
        if (recoveryError_) return MakeError(ErrorKind::Internal, recoveryError_);
        KeyHandle key;
        if (!OpenSettings(subKey_, key)) {
            return MakeError(ErrorKind::Internal, "could not open the system proxy registry key");
        }
        const auto snapshot = ReadSnapshot(key.key);
        if (!snapshot.valid) return MakeError(ErrorKind::Internal, "could not read the current system proxy settings");
        return SystemProxyState{snapshot.enable != 0, ToUtf8(snapshot.server), ToUtf8(snapshot.overrideList)};
    }

    bool Active() const override { return active_; }

    std::string ActiveServer() const override { return server_; }

private:
    core::Result<void> Fail(const char* message, bool recovery = false) {
        if (recovery) recoveryError_ = message;
        if (!active_) lease_.Release();
        return MakeError(ErrorKind::Internal, message);
    }

    core::Result<void> Restore(HKEY key, const Snapshot& original, const Snapshot& applied) {
        const auto restored = RestoreChanges(key, original, applied, gate_);
        if (restored.changed && NotifyAllowed(subKey_)) NotifySettingsChanged();
        if (const auto error = restored.Error()) return Fail(error, true);
        if (!RemoveJournal(journalPath_)) {
            return Fail("system proxy restored but recovery record could not be removed", true);
        }
        return {};
    }

    static std::wstring DefaultJournalPath() {
        wchar_t* appData = nullptr; size_t length = 0;
        if (_wdupenv_s(&appData, &length, L"LOCALAPPDATA") != 0 || !appData) return L"ClashYuka\\system-proxy-recovery.json";
        std::wstring path(appData); std::free(appData);
        return path + L"\\ClashYuka\\system-proxy-recovery.json";
    }
    std::wstring subKey_;
    std::wstring journalPath_;
    std::wstring mutexName_;
    WriteGate gate_;
    OwnershipLease lease_;
    bool active_ = false;
    const char* recoveryError_ = nullptr;
    std::string server_;
};

}  // namespace

std::string BuildWindowsProxyServer(const std::string& host, int port) {
    return "http=" + host + ":" + std::to_string(port) + ";https=" + host + ":" +
           std::to_string(port);
}

std::unique_ptr<SystemProxyController> CreateWindowsSystemProxyController(
    const std::wstring& subKey, const std::wstring& journalPath) {
    return std::make_unique<WindowsSystemProxyController>(subKey, journalPath);
}

std::unique_ptr<SystemProxyController> CreateWindowsSystemProxyControllerForTest(
    const std::wstring& subKey, const std::wstring& journalPath, WriteGate allowWrite) {
    if (subKey.empty() || MutexName(subKey) == MutexName(kDefaultSubKey) || journalPath.empty()) return nullptr;
    return std::make_unique<WindowsSystemProxyController>(subKey, journalPath, std::move(allowWrite));
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
