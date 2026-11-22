#pragma once

#include <string>

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
};

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
