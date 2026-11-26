#include "clashyuka/runtime/settings_store.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <nlohmann/json.hpp>

#include "clashyuka/platform/platform_services.h"
#include "clashyuka/runtime/runtime_config.h"

namespace clashyuka::runtime {
namespace {

namespace fs = std::filesystem;
using core::ErrorKind;
using core::MakeError;

constexpr int kSettingsVersion = 4;

fs::path PathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

bool ReadString(const nlohmann::json& root, const char* key, std::string& value) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return false;
    }
    if (!it->is_string()) {
        return false;
    }
    value = it->get<std::string>();
    return true;
}

bool ReadInt(const nlohmann::json& root, const char* key, int& value) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_number_integer()) {
        return false;
    }
    value = it->get<int>();
    return true;
}

bool ReadBool(const nlohmann::json& root, const char* key, bool& value) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_boolean()) {
        return false;
    }
    value = it->get<bool>();
    return true;
}

core::Result<void> WriteSettingsFile(const fs::path& path, const AppSettings& settings) {
    const fs::path temp = path.string() + ".tmp";

    nlohmann::json root;
    root["version"] = kSettingsVersion;
    root["mihomoPath"] = settings.mihomoPath;
    root["controllerHost"] = settings.controllerHost;
    root["controllerPort"] = settings.controllerPort;
    root["mixedPort"] = settings.mixedPort;
    root["secret"] = settings.secret;
    root["autoStartKernel"] = settings.autoStartKernel;
    root["proxyTestUrl"] = settings.proxyTestUrl;
    root["proxyTestTimeoutMs"] = settings.proxyTestTimeoutMs;
    root["kernelInstallDir"] = settings.kernelInstallDir;
    root["downloadProxy"] = settings.downloadProxy;
    root["enableSystemProxy"] = settings.enableSystemProxy;
    root["closeToTray"] = settings.closeToTray;
    root["windowHotkey"] = settings.windowHotkey;
    root["geoSourcePreset"] = settings.geoSourcePreset;

    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            return MakeError(ErrorKind::ConfigFailed,
                             "could not open the temporary settings file for writing");
        }
        output << root.dump(2) << '\n';
        output.flush();
        if (!output) {
            output.close();
            std::error_code ignored;
            fs::remove(temp, ignored);
            return MakeError(ErrorKind::ConfigFailed, "could not write the settings file");
        }
    }

    std::string replaceError;
    if (!platform::ReplaceFileAtomically(temp.string(), path.string(), &replaceError)) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return MakeError(ErrorKind::ConfigFailed,
                         "could not replace the settings file: " + replaceError);
    }

    // Best effort: keep the file readable only by the current user on POSIX.
    std::error_code ignored;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ignored);
    return {};
}

}  // namespace

std::string WindowHotkey::ToString() const {
    if (!key) {
        return {};
    }
    std::string text;
    if (ctrl) text += "Ctrl+";
    if (alt) text += "Alt+";
    if (shift) text += "Shift+";
    if (win) text += "Win+";
    return text + key;
}

std::optional<WindowHotkey> ParseWindowHotkey(std::string_view text) {
    const auto trim = [](std::string_view piece) {
        const auto start = piece.find_first_not_of(" \t");
        return start == std::string_view::npos
                   ? std::string_view{}
                   : piece.substr(start, piece.find_last_not_of(" \t") - start + 1);
    };
    text = trim(text);
    WindowHotkey hotkey;
    if (text.empty()) {
        return hotkey;
    }
    for (auto split = text.find('+'); split != std::string_view::npos; split = text.find('+')) {
        const auto modifier = trim(text.substr(0, split));
        std::string lower(modifier);
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        bool* flag = nullptr;
        if (lower == "ctrl") flag = &hotkey.ctrl;
        if (lower == "alt") flag = &hotkey.alt;
        if (lower == "shift") flag = &hotkey.shift;
        if (lower == "win") flag = &hotkey.win;
        if (flag == nullptr || *flag) {
            return std::nullopt;
        }
        *flag = true;
        text.remove_prefix(split + 1);
    }
    text = trim(text);
    if (text.size() != 1 || !(hotkey.ctrl || hotkey.alt || hotkey.win)) {
        return std::nullopt;
    }
    char key = text.front();
    if (key >= 'a' && key <= 'z') key = static_cast<char>(key - 'a' + 'A');
    if (!((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))) {
        return std::nullopt;
    }
    hotkey.key = key;
    return hotkey;
}

std::string GenerateLocalSecret() {
    std::random_device device;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string secret;
    secret.reserve(32);
    for (int i = 0; i < 32; ++i) {
        secret.push_back(kHex[device() % 16]);
    }
    return secret;
}

AppSettings DefaultSettings() {
    AppSettings settings;
    settings.secret = GenerateLocalSecret();
    return settings;
}

SettingsLoadResult LoadSettings(const std::string& path) {
    SettingsLoadResult result;
    result.settings = DefaultSettings();

    const fs::path file = PathFromUtf8(path);
    std::error_code existsError;
    const bool exists = fs::exists(file, existsError);
    if (existsError) {
        result.warning = "could not inspect the settings file; using defaults";
        return result;
    }
    if (!exists) {
        result.firstRun = true;
        const core::Result<void> saved = SaveSettings(path, result.settings);
        if (!saved) {
            result.warning = saved.error().message;
        }
        return result;
    }

    std::ifstream input(file, std::ios::binary);
    if (!input) {
        result.warning = "could not read the settings file; using defaults";
        return result;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception&) {
        result.warning = "the settings file is not valid JSON; using defaults";
        return result;
    }
    if (!root.is_object()) {
        result.warning = "the settings file has an unexpected shape; using defaults";
        return result;
    }

    AppSettings settings = result.settings;
    bool partial = false;
    partial |= !ReadString(root, "mihomoPath", settings.mihomoPath);
    partial |= !ReadString(root, "controllerHost", settings.controllerHost);
    partial |= !ReadInt(root, "controllerPort", settings.controllerPort);
    partial |= !ReadInt(root, "mixedPort", settings.mixedPort);
    partial |= !ReadString(root, "secret", settings.secret);
    partial |= !ReadBool(root, "autoStartKernel", settings.autoStartKernel);
    partial |= !ReadString(root, "proxyTestUrl", settings.proxyTestUrl);
    partial |= !ReadInt(root, "proxyTestTimeoutMs", settings.proxyTestTimeoutMs);
    partial |= !ReadString(root, "kernelInstallDir", settings.kernelInstallDir);
    partial |= !ReadString(root, "downloadProxy", settings.downloadProxy);
    partial |= !ReadBool(root, "enableSystemProxy", settings.enableSystemProxy);
    // Optional for settings files saved before desktop controls were added.
    if (root.contains("closeToTray")) {
        partial |= !ReadBool(root, "closeToTray", settings.closeToTray);
    }
    if (root.contains("windowHotkey")) {
        std::string text;
        if (ReadString(root, "windowHotkey", text)) {
            if (const auto parsed = ParseWindowHotkey(text)) {
                settings.windowHotkey = parsed->ToString();
            } else {
                partial = true;
            }
        } else {
            partial = true;
        }
    }

    if (root.contains("geoSourcePreset")) {
        std::string id;
        if (ReadString(root, "geoSourcePreset", id) && FindGeoSourcePreset(id)) {
            settings.geoSourcePreset = id;
        } else {
            partial = true;
        }
    }

    if (settings.secret.empty()) {
        settings.secret = GenerateLocalSecret();
        partial = true;
    }

    const core::Result<void> valid = ValidateSettings(settings);
    if (!valid) {
        result.warning = "the settings file contains invalid values; using defaults";
        return result;
    }

    result.settings = std::move(settings);
    if (partial) {
        result.warning = "some settings were missing or invalid; defaults were applied";
    }
    return result;
}

core::Result<void> ValidateSettings(const AppSettings& settings) {
    if (settings.controllerHost.empty()) {
        return MakeError(ErrorKind::InvalidConfig, "the controller host must not be empty");
    }
    if (settings.controllerHost == "0.0.0.0" || settings.controllerHost == "::" ||
        settings.controllerHost == "*") {
        return MakeError(ErrorKind::InvalidConfig,
                         "the controller host must not be a wildcard address");
    }
    if (settings.controllerHost.find('\n') != std::string::npos ||
        settings.controllerHost.find('\r') != std::string::npos) {
        return MakeError(ErrorKind::InvalidConfig, "the controller host is invalid");
    }
    if (settings.controllerPort <= 0 || settings.controllerPort > 65535) {
        return MakeError(ErrorKind::InvalidConfig, "the controller port is out of range");
    }
    if (settings.mixedPort <= 0 || settings.mixedPort > 65535) {
        return MakeError(ErrorKind::InvalidConfig, "the mixed port is out of range");
    }
    if (settings.mixedPort == settings.controllerPort) {
        return MakeError(ErrorKind::InvalidConfig,
                         "the mixed port and controller port must differ");
    }
    if (settings.secret.size() > 256 || settings.secret.find('\n') != std::string::npos ||
        settings.secret.find('\r') != std::string::npos) {
        return MakeError(ErrorKind::InvalidConfig, "the API secret is invalid");
    }
    if (settings.proxyTestUrl.empty() ||
        (settings.proxyTestUrl.rfind("http://", 0) != 0 &&
         settings.proxyTestUrl.rfind("https://", 0) != 0) ||
        settings.proxyTestUrl.find('\n') != std::string::npos ||
        settings.proxyTestUrl.find('\r') != std::string::npos) {
        return MakeError(ErrorKind::InvalidConfig,
                         "the proxy delay test URL must be an HTTP(S) URL");
    }
    if (settings.proxyTestTimeoutMs < 1000 || settings.proxyTestTimeoutMs > 60000) {
        return MakeError(ErrorKind::InvalidConfig,
                         "the proxy delay timeout must be between 1000 and 60000 ms");
    }
    if (!FindGeoSourcePreset(settings.geoSourcePreset)) {
        return MakeError(ErrorKind::InvalidConfig, "the Geo data download source is invalid");
    }
    if (!ParseWindowHotkey(settings.windowHotkey)) {
        return MakeError(ErrorKind::InvalidConfig, "the global window shortcut is invalid");
    }
    if (!settings.downloadProxy.empty() &&
        settings.downloadProxy.rfind("http://", 0) != 0 &&
        settings.downloadProxy.rfind("https://", 0) != 0) {
        return MakeError(ErrorKind::InvalidConfig,
                         "the download proxy must be an http:// or https:// URL");
    }
    return {};
}

core::Result<void> SaveSettings(const std::string& path, const AppSettings& settings) {
    const core::Result<void> valid = ValidateSettings(settings);
    if (!valid) {
        return valid;
    }

    const fs::path file = PathFromUtf8(path);
    if (file.has_parent_path()) {
        std::error_code createError;
        fs::create_directories(file.parent_path(), createError);
        if (createError) {
            return MakeError(ErrorKind::ConfigFailed,
                             "could not create the settings directory: " +
                                 createError.message());
        }
    }
    return WriteSettingsFile(file, settings);
}

}  // namespace clashyuka::runtime
