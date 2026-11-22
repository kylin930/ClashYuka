#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "clashyuka/core/result.h"

namespace clashyuka::runtime {

struct KernelAsset {
    std::string name;
    std::string url;
    std::uint64_t size = 0;
    // "sha256:<hex>" as returned by the GitHub API, or empty.
    std::string digest;
};

struct KernelRelease {
    std::string tag;
    std::string name;
    std::string publishedAt;
    std::string htmlUrl;
    bool prerelease = false;
    std::vector<KernelAsset> assets;
};

enum class ReleasePlatform {
    WindowsAmd64,
    WindowsArm64,
    LinuxAmd64,
    LinuxArm64,
    MacAmd64,
    MacArm64,
};

ReleasePlatform CurrentReleasePlatform();
const char* ReleasePlatformName(ReleasePlatform platform);

core::Result<KernelRelease> ParseKernelRelease(const nlohmann::json& json);
core::Result<KernelAsset> SelectKernelAsset(const KernelRelease& release,
                                            ReleasePlatform platform);
bool IsStableRelease(const KernelRelease& release);

// Lowercases and strips an optional "sha256:" prefix. Returns an empty string
// when the value is not a 64-character hex digest.
std::string NormalizeSha256(std::string value);

}  // namespace clashyuka::runtime
