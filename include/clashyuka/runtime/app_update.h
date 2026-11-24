#pragma once

#include <string_view>

#include "clashyuka/platform/http_download.h"

namespace clashyuka::runtime {

enum class AppUpdateStatus {
    Unavailable, Idle, Checking, Cancelling, Cancelled, Current, Available, NoAsset, Failed,
};

struct AppUpdateSnapshot {
    AppUpdateStatus status = AppUpdateStatus::Unavailable;
    std::string latestVersion;
    std::string assetName;
    std::string releasePage;
    std::string message;
};

using AppUpdateTransport = std::function<core::Result<platform::HttpTextResponse>(
    const std::string&, const platform::HttpsTextLimits&, std::atomic<bool>&)>;

std::string_view AppBuildVersion();
AppUpdateSnapshot InitialAppUpdate();
AppUpdateSnapshot CheckOfficialAppUpdate(std::atomic<bool>& cancel);

// GitHub's latest stable release for a build-selected owner/repo.
AppUpdateSnapshot CheckAppUpdate(std::string_view repository, std::string_view currentVersion,
                                 std::string_view platformName, std::string_view architecture,
                                 const AppUpdateTransport& fetch, std::atomic<bool>& cancel);

}  // namespace clashyuka::runtime
