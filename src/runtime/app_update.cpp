#include "clashyuka/runtime/app_update.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>

#include <nlohmann/json.hpp>

namespace clashyuka::runtime {
namespace {

using Status = AppUpdateStatus;
using Json = nlohmann::json;
using Version = std::array<unsigned, 3>;
constexpr platform::HttpsTextLimits kLimits{512 * 1024, std::chrono::seconds(15), false};
constexpr std::string_view kRepository = CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY;

AppUpdateSnapshot WithStatus(Status status, std::string message = {}) {
    return {status, {}, {}, {}, std::move(message)};
}

bool IsAlphaNumeric(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

bool ValidRepository(std::string_view repository) {
    const auto slash = repository.find('/');
    if (slash == repository.npos || slash == 0 || slash > 39) {
        return false;
    }
    const auto owner = repository.substr(0, slash);
    const auto name = repository.substr(slash + 1);
    return IsAlphaNumeric(owner.front()) && IsAlphaNumeric(owner.back()) &&
           std::ranges::all_of(owner, [](char ch) { return IsAlphaNumeric(ch) || ch == '-'; }) &&
           !name.empty() && name.size() <= 100 && name != "." && name != ".." &&
           std::ranges::all_of(name, [](char ch) {
               return IsAlphaNumeric(ch) || ch == '-' || ch == '_' || ch == '.';
           });
}

std::optional<Version> ParseVersion(std::string_view value) {
    if (value.starts_with('v')) {
        value.remove_prefix(1);
    }
    Version version{};
    for (std::size_t index = 0; index < version.size(); ++index) {
        const bool last = index + 1 == version.size();
        const auto dot = value.find('.');
        if ((dot == value.npos) != last) {
            return std::nullopt;
        }
        const auto part = value.substr(0, dot);
        if (part.empty() || (part.size() > 1 && part.front() == '0')) {
            return std::nullopt;
        }
        const auto end = part.data() + part.size();
        const auto [ptr, error] = std::from_chars(part.data(), end, version[index]);
        if (error != std::errc{} || ptr != end) {
            return std::nullopt;
        }
        if (!last) {
            value.remove_prefix(dot + 1);
        }
    }
    return version;
}

std::string_view Text(const Json& object, const char* key) {
    const auto field = object.find(key);
    if (field != object.end()) {
        if (const auto* value = field->get_ptr<const std::string*>()) {
            return *value;
        }
    }
    return {};
}

bool IsBoolean(const Json& object, const char* key, bool expected) {
    const auto field = object.find(key);
    return field != object.end() && *field == expected;
}

bool ValidAsset(const Json& asset, const std::string& downloadBase, std::string_view tag,
                std::string_view platformName, std::string_view architecture) {
    const auto name = Text(asset, "name");
    if (name.empty() || name.size() > 160 || Text(asset, "state") != "uploaded") {
        return false;
    }
    const auto size = asset.find("size");
    if (size == asset.end() || !size->is_number_unsigned() || size->get<std::uint64_t>() == 0) {
        return false;
    }
    const auto dot = name.rfind('.');
    if (dot == name.npos) {
        return false;
    }
    const auto ext = name.substr(dot);
    if (platformName != "windows" || (ext != ".zip" && ext != ".msi" && ext != ".exe")) {
        return false;
    }
    auto version = tag;
    if (version.starts_with('v')) {
        version.remove_prefix(1);
    }
    const auto target = std::string(platformName) + "-" + std::string(architecture);
    const auto stem = name.substr(0, dot);
    if (architecture.empty() ||
        (stem != "ClashYuka-" + target && stem != "ClashYuka-" + std::string(tag) + "-" + target &&
         stem != "ClashYuka-" + std::string(version) + "-" + target)) {
        return false;
    }
    return Text(asset, "browser_download_url") == downloadBase + std::string(name);
}

std::string_view BuildArchitecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return {};
#endif
}

#ifdef _WIN32
constexpr std::string_view kBuildPlatform = "windows";
#else
constexpr std::string_view kBuildPlatform;
#endif

struct ParsingStopped {};

}  // namespace

std::string_view AppBuildVersion() { return CLASHYUKA_BUILD_VERSION; }

AppUpdateSnapshot InitialAppUpdate() {
    if (!ValidRepository(kRepository)) {
        return WithStatus(Status::Unavailable, "No official release source is configured for this build.");
    }
    if (kBuildPlatform.empty() || BuildArchitecture().empty() || !platform::HttpsTextAvailable()) {
        return WithStatus(Status::Unavailable, "Update checks are unavailable on this platform.");
    }
    return WithStatus(Status::Idle);
}

AppUpdateSnapshot CheckOfficialAppUpdate(std::atomic<bool>& cancel) {
    auto initial = InitialAppUpdate();
    if (initial.status != Status::Idle) {
        return initial;
    }
    return CheckAppUpdate(kRepository, AppBuildVersion(), kBuildPlatform, BuildArchitecture(),
                          [](const std::string& url, const platform::HttpsTextLimits& limits,
                             std::atomic<bool>& stop) {
                              return platform::HttpGetBoundedHttpsText(url, {}, limits, stop);
                          }, cancel);
}

AppUpdateSnapshot CheckAppUpdate(std::string_view repository, std::string_view currentVersion,
                                 std::string_view platformName, std::string_view architecture,
                                 const AppUpdateTransport& fetch, std::atomic<bool>& cancel) {
    if (cancel.load()) {
        return WithStatus(Status::Cancelled);
    }
    if (!ValidRepository(repository)) {
        return WithStatus(Status::Unavailable, "No official release source is configured for this build.");
    }
    const auto current = ParseVersion(currentVersion);
    if (!current) {
        return WithStatus(Status::Failed, "The current app version is invalid.");
    }

    const std::string repo(repository);
    const auto response = fetch("https://api.github.com/repos/" + repo + "/releases/latest",
                                kLimits, cancel);
    if (cancel.load() || (!response && response.error().kind == core::ErrorKind::Cancelled)) {
        return WithStatus(Status::Cancelled);
    }
    if (!response) {
        return WithStatus(Status::Failed, response.error().kind == core::ErrorKind::Timeout
                                             ? "The update check timed out. Try again."
                                             : "Could not reach the official release source. Try again.");
    }
    if (response.value().status != 200) {
        return WithStatus(Status::Failed, "The release source returned HTTP " +
                                              std::to_string(response.value().status) + ". Try again.");
    }
    if (response.value().body.size() > kLimits.maxBytes) {
        return WithStatus(Status::Failed, "The release response is too large.");
    }

    Json release;
    try {
        release = Json::parse(response.value().body, [&](int depth, Json::parse_event_t, Json&) {
            if (cancel.load() || depth > 32) {
                throw ParsingStopped{};
            }
            return true;
        }, false);
    } catch (const ParsingStopped&) {
        if (cancel.load()) {
            return WithStatus(Status::Cancelled);
        }
    }
    const auto tag = Text(release, "tag_name");
    const auto latest = ParseVersion(tag);
    const auto assets = release.find("assets");
    if (!latest || !IsBoolean(release, "draft", false) || !IsBoolean(release, "prerelease", false) ||
        assets == release.end() || !assets->is_array()) {
        return WithStatus(Status::Failed, "The official release metadata is invalid.");
    }
    auto result = WithStatus(Status::Current);
    result.latestVersion = std::string(tag);
    if (*latest > *current) {
        result.status = Status::NoAsset;
        const auto base = "https://github.com/" + repo + "/releases/";
        const auto downloadBase = base + "download/" + std::string(tag) + "/";
        for (const auto& asset : *assets) {
            if (cancel.load()) {
                return WithStatus(Status::Cancelled);
            }
            if (ValidAsset(asset, downloadBase, tag, platformName, architecture)) {
                result.status = Status::Available;
                result.assetName = Text(asset, "name");
                result.releasePage = base + "tag/" + std::string(tag);
                break;
            }
        }
    }
    return cancel.load() ? WithStatus(Status::Cancelled) : result;
}

}  // namespace clashyuka::runtime
