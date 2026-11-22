#pragma once

#include <memory>
#include <string>

namespace clashyuka::platform {

// Platform specific services (system proxy, autostart, privileges, TUN, ...).
// P0 only exposes read-only environment information; no mutation is implemented.
class PlatformServices {
public:
    virtual ~PlatformServices() = default;

    PlatformServices(const PlatformServices&) = delete;
    PlatformServices& operator=(const PlatformServices&) = delete;

    // Human readable platform name: "Windows", "macOS" or "Linux".
    virtual std::string Name() const = 0;

    // True when the process has administrative/root privileges.
    virtual bool IsElevated() const = 0;

protected:
    PlatformServices() = default;
};

// Creates the implementation matching the platform this binary is built for.
std::unique_ptr<PlatformServices> CreatePlatformServices();

// Replaces target with temp as atomically as the platform allows. Both paths are
// UTF-8. On failure, error receives a short diagnostic; temp may remain.
bool ReplaceFileAtomically(const std::string& tempPath, const std::string& targetPath,
                           std::string* error);

}  // namespace clashyuka::platform
