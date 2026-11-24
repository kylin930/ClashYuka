#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "clashyuka/core/result.h"

namespace clashyuka::runtime {

// Persisted, non-profile runtime preferences. The secret is stored as plain
// text in the per-user application directory (the same trust boundary as the
// Mihomo config itself); it is never written to the log. A platform credential
// vault is deliberately out of scope for P2.2.
struct AppSettings {
    std::string mihomoPath;
    std::string controllerHost = "127.0.0.1";
    int controllerPort = 9090;
    int mixedPort = 7897;
    std::string secret;
    bool autoStartKernel = false;

    // Proxy page delay test. The default avoids relying on a provider URL.
    std::string proxyTestUrl = "https://www.gstatic.com/generate_204";
    int proxyTestTimeoutMs = 5000;

    // Kernel installer. Empty install root means "<settings dir>/kernels".
    std::string kernelInstallDir;
    // Empty means "use the system proxy".
    std::string downloadProxy;

    // Apply the OS HTTP/HTTPS proxy while the kernel is running.
    bool enableSystemProxy = false;

    bool closeToTray = false;
    // Empty disables the global window shortcut.
    std::string windowHotkey;
};

struct WindowHotkey {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    char key = 0;

    std::string ToString() const;
};

// Empty input is a valid disabled shortcut. Invalid combinations return nullopt.
std::optional<WindowHotkey> ParseWindowHotkey(std::string_view text);

struct SettingsLoadResult {
    AppSettings settings;
    bool firstRun = false;
    // Non-empty when the file was unreadable/invalid and defaults were used.
    std::string warning;
};

// Returns defaults, including a freshly generated local secret.
AppSettings DefaultSettings();

// Loads settings from path. A missing file creates the defaults on disk; a
// corrupt file is preserved (never overwritten) and defaults are returned.
SettingsLoadResult LoadSettings(const std::string& path);

// Validates fields without touching the filesystem.
core::Result<void> ValidateSettings(const AppSettings& settings);

// Writes through a temporary file and an atomic replace. Creates the parent
// directory when needed.
core::Result<void> SaveSettings(const std::string& path, const AppSettings& settings);

// Exposed for tests and UI diagnostics. 32 lowercase hex characters.
std::string GenerateLocalSecret();

}  // namespace clashyuka::runtime
