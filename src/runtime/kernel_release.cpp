#include "clashyuka/runtime/kernel_release.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace clashyuka::runtime {
namespace {

using core::ErrorKind;
using core::MakeError;
using Json = nlohmann::json;

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

const char* PlatformPrefix(ReleasePlatform platform) {
    switch (platform) {
        case ReleasePlatform::WindowsAmd64:
            return "mihomo-windows-amd64";
        case ReleasePlatform::WindowsArm64:
            return "mihomo-windows-arm64";
        case ReleasePlatform::LinuxAmd64:
            return "mihomo-linux-amd64";
        case ReleasePlatform::LinuxArm64:
            return "mihomo-linux-arm64";
        case ReleasePlatform::MacAmd64:
            return "mihomo-darwin-amd64";
        case ReleasePlatform::MacArm64:
            return "mihomo-darwin-arm64";
    }
    return "";
}

bool IsWindows(ReleasePlatform platform) {
    return platform == ReleasePlatform::WindowsAmd64 ||
           platform == ReleasePlatform::WindowsArm64;
}

bool IsArm64(ReleasePlatform platform) {
    return platform == ReleasePlatform::WindowsArm64 ||
           platform == ReleasePlatform::LinuxArm64 ||
           platform == ReleasePlatform::MacArm64;
}

enum class Variant { Compatible, V1, Plain, GoGeneric, V2, V3, Other };

Variant Classify(const std::string& rest) {
    if (rest.empty()) {
        return Variant::Plain;
    }
    if (rest == "-compatible") {
        return Variant::Compatible;
    }
    if (rest == "-v1" || rest.rfind("-v1-go", 0) == 0) {
        return Variant::V1;
    }
    if (rest.rfind("-v2", 0) == 0) {
        return Variant::V2;
    }
    if (rest.rfind("-v3", 0) == 0) {
        return Variant::V3;
    }
    if (rest.rfind("-go", 0) == 0) {
        return Variant::GoGeneric;
    }
    return Variant::Other;
}

int Rank(Variant variant, bool arm64) {
    if (arm64) {
        switch (variant) {
            case Variant::Compatible:
            case Variant::Plain:
            case Variant::V1:
                return 0;
            case Variant::GoGeneric:
                return 1;
            default:
                return -1;
        }
    }
    switch (variant) {
        case Variant::Compatible:
            return 0;
        case Variant::V1:
            return 1;
        case Variant::Plain:
            return 2;
        case Variant::GoGeneric:
            return 3;
        default:
            return -1;
    }
}

}  // namespace

ReleasePlatform CurrentReleasePlatform() {
#if defined(_WIN32)
#if defined(_M_ARM64)
    return ReleasePlatform::WindowsArm64;
#else
    return ReleasePlatform::WindowsAmd64;
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return ReleasePlatform::MacArm64;
#else
    return ReleasePlatform::MacAmd64;
#endif
#else
#if defined(__aarch64__) || defined(__arm64__)
    return ReleasePlatform::LinuxArm64;
#else
    return ReleasePlatform::LinuxAmd64;
#endif
#endif
}

const char* ReleasePlatformName(ReleasePlatform platform) {
    switch (platform) {
        case ReleasePlatform::WindowsAmd64:
            return "windows-amd64";
        case ReleasePlatform::WindowsArm64:
            return "windows-arm64";
        case ReleasePlatform::LinuxAmd64:
            return "linux-amd64";
        case ReleasePlatform::LinuxArm64:
            return "linux-arm64";
        case ReleasePlatform::MacAmd64:
            return "darwin-amd64";
        case ReleasePlatform::MacArm64:
            return "darwin-arm64";
    }
    return "unknown";
}

core::Result<KernelRelease> ParseKernelRelease(const Json& json) {
    if (!json.is_object()) {
        return MakeError(ErrorKind::TypeMismatch, "release payload is not an object");
    }
    const auto tag = json.find("tag_name");
    if (tag == json.end() || !tag->is_string() || tag->get<std::string>().empty()) {
        return MakeError(ErrorKind::MissingField, "release has no tag_name");
    }

    KernelRelease release;
    release.tag = tag->get<std::string>();
    if (const auto it = json.find("name"); it != json.end() && it->is_string()) {
        release.name = it->get<std::string>();
    }
    if (const auto it = json.find("published_at"); it != json.end() && it->is_string()) {
        release.publishedAt = it->get<std::string>();
    }
    if (const auto it = json.find("html_url"); it != json.end() && it->is_string()) {
        release.htmlUrl = it->get<std::string>();
    }
    if (const auto it = json.find("prerelease"); it != json.end() && it->is_boolean()) {
        release.prerelease = it->get<bool>();
    }

    const auto assets = json.find("assets");
    if (assets == json.end() || !assets->is_array()) {
        return MakeError(ErrorKind::MissingField, "release has no assets array");
    }
    for (const auto& item : *assets) {
        if (!item.is_object()) {
            continue;
        }
        const auto name = item.find("name");
        const auto url = item.find("browser_download_url");
        if (name == item.end() || !name->is_string() || url == item.end() ||
            !url->is_string()) {
            continue;
        }
        KernelAsset asset;
        asset.name = name->get<std::string>();
        asset.url = url->get<std::string>();
        if (const auto size = item.find("size"); size != item.end() && size->is_number()) {
            const double number = size->get<double>();
            asset.size = number > 0 ? static_cast<std::uint64_t>(number) : 0;
        }
        if (const auto digest = item.find("digest"); digest != item.end() &&
                                                     digest->is_string()) {
            asset.digest = digest->get<std::string>();
        }
        release.assets.push_back(std::move(asset));
    }
    return release;
}

bool IsStableRelease(const KernelRelease& release) {
    return !release.prerelease;
}

core::Result<KernelAsset> SelectKernelAsset(const KernelRelease& release,
                                            ReleasePlatform platform) {
    const std::string prefix = PlatformPrefix(platform);
    const bool windows = IsWindows(platform);
    const bool arm64 = IsArm64(platform);

    const KernelAsset* best = nullptr;
    int bestRank = 1000;
    for (const KernelAsset& asset : release.assets) {
        const std::string lower = Lower(asset.name);
        if (lower.find("source") != std::string::npos || asset.url.empty() ||
            asset.size == 0) {
            continue;
        }
        if (lower.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string extension = windows ? ".zip" : ".gz";
        if (!EndsWith(lower, extension)) {
            continue;
        }

        std::string rest = lower.substr(prefix.size(), lower.size() - prefix.size() -
                                                          extension.size());
        const std::string tagSuffix = "-" + Lower(release.tag);
        if (EndsWith(rest, tagSuffix)) {
            rest.resize(rest.size() - tagSuffix.size());
        } else {
            const std::size_t version = rest.rfind("-v");
            if (version != std::string::npos && version + 2 < rest.size() &&
                std::isdigit(static_cast<unsigned char>(rest[version + 2])) != 0) {
                rest.resize(version);
            }
        }

        const int rank = Rank(Classify(rest), arm64);
        if (rank < 0) {
            continue;
        }
        if (rank < bestRank) {
            bestRank = rank;
            best = &asset;
        }
    }

    if (best == nullptr) {
        return MakeError(ErrorKind::KernelNotFound,
                         "the release has no compatible asset for " +
                             std::string(ReleasePlatformName(platform)));
    }
    return *best;
}

std::string NormalizeSha256(std::string value) {
    value = Lower(std::move(value));
    if (value.rfind("sha256:", 0) == 0) {
        value = value.substr(7);
    }
    if (value.size() != 64) {
        return {};
    }
    for (const char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return {};
        }
    }
    return value;
}

}  // namespace clashyuka::runtime
