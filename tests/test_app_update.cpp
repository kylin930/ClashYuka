#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "clashyuka/runtime/app_update.h"

namespace {
using namespace clashyuka;
using runtime::AppUpdateStatus;
using Json = nlohmann::json;
constexpr auto kRepo = "ExampleOwner/ClashYuka";
constexpr auto kAsset = "ClashYuka-v0.10.0-windows-x64.zip";
constexpr auto kDownload = "https://github.com/ExampleOwner/ClashYuka/releases/download/v0.10.0/";

Json Release() {
    return {{"tag_name", "v0.10.0"}, {"draft", false}, {"prerelease", false},
            {"html_url", "https://untrusted.example/release"},
            {"assets", Json::array({{{"name", kAsset}, {"state", "uploaded"}, {"size", 42},
                                     {"browser_download_url", std::string(kDownload) + kAsset}}})}};
}

runtime::AppUpdateSnapshot Check(const Json& release, std::string_view current = "0.2.0",
                                 std::string_view platformName = "windows",
                                 std::string_view architecture = "x64") {
    std::atomic<bool> cancel{false};
    return runtime::CheckAppUpdate(kRepo, current, platformName, architecture,
        [&](const std::string& url, const platform::HttpsTextLimits& limits, std::atomic<bool>&) {
            CHECK(url == "https://api.github.com/repos/ExampleOwner/ClashYuka/releases/latest");
            CHECK(limits.maxBytes == 512 * 1024);
            CHECK(limits.timeout == std::chrono::seconds(15));
            CHECK_FALSE(limits.followRedirects);
            return platform::HttpTextResponse{200, release.dump()};
        }, cancel);
}
}  // namespace

TEST_CASE("Application release checks compare stable versions and build official links") {
    const auto update = Check(Release());
    CHECK(update.status == AppUpdateStatus::Available);
    CHECK(update.latestVersion == "v0.10.0");
    CHECK(update.assetName == kAsset);
    CHECK(update.releasePage == "https://github.com/ExampleOwner/ClashYuka/releases/tag/v0.10.0");
    for (const auto version : {"0.10.0", "0.11.0", "v1.0.0"}) {
        const auto current = Check(Release(), version);
        CHECK(current.status == AppUpdateStatus::Current);
        CHECK(current.releasePage.empty());
    }
    for (const auto tag : {"v0.10.0-rc.1", "v00.10.0", "v0.10", "v42949672960.0.0", "../../path"}) {
        auto release = Release();
        release["tag_name"] = tag;
        CHECK(Check(release).status == AppUpdateStatus::Failed);
    }
    for (const auto key : {"draft", "prerelease"}) {
        auto release = Release();
        release[key] = true;
        CHECK(Check(release).status == AppUpdateStatus::Failed);
    }
}

TEST_CASE("Application releases require a compatible uploaded official asset") {
    auto release = Release();
    CHECK(Check(release, "0.2.0", "windows", "arm64").status == AppUpdateStatus::NoAsset);
    CHECK(Check(release, "0.2.0", "linux", "x64").status == AppUpdateStatus::NoAsset);
    SUBCASE("empty asset list") { release["assets"] = Json::array(); }
    SUBCASE("empty asset") { release["assets"][0]["size"] = 0; }
    SUBCASE("asset still uploading") { release["assets"][0]["state"] = "new"; }
    SUBCASE("asset from another repo") {
        release["assets"][0]["browser_download_url"] =
            "https://github.com/Other/ClashYuka/releases/download/v0.10.0/" + std::string(kAsset);
    }
    SUBCASE("non-package file") {
        release["assets"][0]["name"] = "ClashYuka-v0.10.0-windows-x64.txt";
        release["assets"][0]["browser_download_url"] =
            std::string(kDownload) + "ClashYuka-v0.10.0-windows-x64.txt";
    }
    const auto result = Check(release);
    CHECK(result.status == AppUpdateStatus::NoAsset);
    CHECK(result.latestVersion == "v0.10.0");
    CHECK(result.releasePage.empty());
}

TEST_CASE("Application release checks handle unavailability, failures and cancellation") {
    using core::ErrorKind;
    std::atomic<bool> cancel{false};
    bool fetched = false;
    auto expected = AppUpdateStatus::Failed;
    runtime::AppUpdateTransport fetch = [&](const auto&, const auto&, auto& stop)
        -> core::Result<platform::HttpTextResponse> {
        fetched = true;
        SUBCASE("TLS or network failure") {
            return core::MakeError(ErrorKind::DownloadFailed, "network error");
        }
        SUBCASE("timeout") {
            return core::MakeError(ErrorKind::Timeout, "timeout");
        }
        SUBCASE("HTTP failure") { return platform::HttpTextResponse{403, {}}; }
        SUBCASE("redirect") { return platform::HttpTextResponse{301, Release().dump()}; }
        SUBCASE("malformed JSON") { return platform::HttpTextResponse{200, "{invalid"}; }
        SUBCASE("oversized response") {
            return platform::HttpTextResponse{200, std::string(512 * 1024 + 1, 'x')};
        }
        SUBCASE("cancel wins over success") {
            expected = AppUpdateStatus::Cancelled;
            stop.store(true);
            return platform::HttpTextResponse{200, Release().dump()};
        }
        SUBCASE("transport reports cancellation") {
            expected = AppUpdateStatus::Cancelled;
            return core::MakeError(ErrorKind::Cancelled, "cancelled");
        }
        return platform::HttpTextResponse{200, {}};
    };
    for (const auto repo : {"", "https://github.com/ExampleOwner/ClashYuka", "owner/..", "owner/repo?x"}) {
        CHECK(runtime::CheckAppUpdate(repo, "0.2.0", "windows", "x64", fetch, cancel).status ==
              AppUpdateStatus::Unavailable);
    }
    cancel.store(true);
    CHECK(runtime::CheckAppUpdate(kRepo, "0.2.0", "windows", "x64", fetch, cancel).status ==
          AppUpdateStatus::Cancelled);
    CHECK_FALSE(fetched);
    cancel.store(false);
    const auto result = runtime::CheckAppUpdate(kRepo, "0.2.0", "windows", "x64", fetch, cancel);
    CHECK(fetched);
    CHECK(result.status == expected);
    CHECK(result.releasePage.empty());
}
