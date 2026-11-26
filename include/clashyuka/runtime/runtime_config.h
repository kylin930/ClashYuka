#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "clashyuka/core/result.h"

namespace clashyuka::runtime {

struct GeoSourcePreset {
    std::string_view id;
    std::string_view label;
    std::string_view baseUrl;
};

inline constexpr std::array kGeoSourcePresets{
    GeoSourcePreset{"profile", "Use profile or Mihomo default", {}},
    GeoSourcePreset{"metacubex-release", "MetaCubeX (GitHub Release)",
                    "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/"},
    GeoSourcePreset{"metacubex-raw", "MetaCubeX (GitHub Raw)",
                    "https://raw.githubusercontent.com/MetaCubeX/meta-rules-dat/release/"},
    GeoSourcePreset{"metacubex-jsdelivr", "MetaCubeX (jsDelivr)",
                    "https://cdn.jsdelivr.net/gh/MetaCubeX/meta-rules-dat@release/"},
};

inline constexpr const GeoSourcePreset* FindGeoSourcePreset(std::string_view id) {
    for (const auto& preset : kGeoSourcePresets) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

// Minimal, P2.2-only runtime configuration. It is written to the application
// data directory and always binds the controller and proxy to loopback.
struct RuntimeConfigDefinition {
    std::string host = "127.0.0.1";
    int port = 9090;
    int mixedPort = 7897;
    std::string secret;
    std::string mode = "rule";
    std::string logLevel = "info";
};

// Values actually used by the kernel after loading/creating the file.
struct RuntimeConfigInfo {
    std::string host = "127.0.0.1";
    int port = 9090;
    int mixedPort = 0;
    std::string secret;
    bool generated = false;
};

core::Result<void> ValidateRuntimeConfigDefinition(const RuntimeConfigDefinition& definition);

// Renders a complete YAML document with the ClashYuka marker header.
std::string RenderRuntimeConfig(const RuntimeConfigDefinition& definition);

// Ensures a usable runtime config exists at path.
// - Missing file: generates the app-owned file.
// - App-owned file (marker present) with different settings: regenerated.
// - User-provided file (no marker): validated and adopted, never overwritten.
core::Result<RuntimeConfigInfo> EnsureRuntimeConfig(const std::string& path,
                                                    const RuntimeConfigDefinition& definition);

// A unique, session-owned launch copy. Removing it never touches the source.
struct RuntimeConfigCopy {
    explicit RuntimeConfigCopy(std::filesystem::path directory);
    ~RuntimeConfigCopy();
    RuntimeConfigCopy(const RuntimeConfigCopy&) = delete;
    RuntimeConfigCopy& operator=(const RuntimeConfigCopy&) = delete;

    std::filesystem::path directory;
    std::string configPath;
    std::string workingDirectory;
    RuntimeConfigInfo info;
};

core::Result<std::unique_ptr<RuntimeConfigCopy>> MakeProfileRuntimeCopy(
    const std::string& workingDirectory, std::string_view bytes,
    const RuntimeConfigDefinition& definition, std::string_view geoSourcePreset = "profile");

core::Result<std::unique_ptr<RuntimeConfigCopy>> MakeLegacyRuntimeCopy(
    const std::string& workingDirectory, const std::string& configPath,
    const RuntimeConfigDefinition& definition, std::string_view geoSourcePreset = "profile");

}  // namespace clashyuka::runtime
