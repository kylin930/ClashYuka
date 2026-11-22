#include "clashyuka/runtime/kernel_installer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <nlohmann/json.hpp>

#include "clashyuka/platform/http_download.h"
#include "clashyuka/runtime/sha256.h"

namespace clashyuka::runtime {
namespace {

namespace fs = std::filesystem;
using core::ErrorKind;
using core::MakeError;

constexpr char kReleaseApi[] =
    "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest";

fs::path PathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string PathToUtf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string RandomHex(std::size_t length) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device device;
    std::string value;
    value.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        value.push_back(kHex[device() % 16]);
    }
    return value;
}

std::string SanitizeVersion(const std::string& tag) {
    std::string value;
    value.reserve(tag.size());
    for (const unsigned char c : tag) {
        if (std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_') {
            value.push_back(static_cast<char>(c));
        } else {
            value.push_back('-');
        }
    }
    while (!value.empty() && (value.front() == '.' || value.front() == '-')) {
        value.erase(value.begin());
    }
    return value.empty() ? "kernel-" + RandomHex(6) : value;
}

std::string ExecutableName() {
#ifdef _WIN32
    return "mihomo.exe";
#else
    return "mihomo";
#endif
}

void Report(const KernelInstaller::ProgressHandler& progress, InstallStage stage,
            std::uint64_t downloaded, std::uint64_t total, std::string message = {}) {
    if (!progress) {
        return;
    }
    InstallProgress value;
    value.stage = stage;
    value.downloaded = downloaded;
    value.total = total;
    value.message = std::move(message);
    progress(value);
}

class PlatformTransport final : public KernelHttpTransport {
public:
    core::Result<HttpTextResult> GetText(
        const std::string& url,
        const std::vector<std::pair<std::string, std::string>>& headers,
        const std::string& proxyUrl, std::atomic<bool>& cancel) override {
        core::Result<platform::HttpTextResponse> response =
            platform::HttpGetText(url, headers, proxyUrl, cancel);
        if (!response) {
            return response.error();
        }
        HttpTextResult result;
        result.status = response.value().status;
        result.body = std::move(response.value().body);
        return result;
    }

    core::Result<void> DownloadFile(const std::string& url, const std::string& targetPath,
                                    const std::string& proxyUrl, const Progress& progress,
                                    std::atomic<bool>& cancel) override {
        return platform::HttpDownloadFile(url, targetPath, proxyUrl, progress, cancel);
    }
};

struct StagingGuard {
    fs::path path;
    ~StagingGuard() {
        if (path.empty()) {
            return;
        }
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

}  // namespace

const char* InstallStageName(InstallStage stage) {
    switch (stage) {
        case InstallStage::Idle:
            return "Idle";
        case InstallStage::Checking:
            return "Checking";
        case InstallStage::Downloading:
            return "Downloading";
        case InstallStage::Verifying:
            return "Verifying";
        case InstallStage::Extracting:
            return "Extracting";
        case InstallStage::Installing:
            return "Installing";
        case InstallStage::Completed:
            return "Completed";
        case InstallStage::Failed:
            return "Failed";
        case InstallStage::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

std::unique_ptr<KernelHttpTransport> CreatePlatformKernelTransport() {
    return std::make_unique<PlatformTransport>();
}

std::string DefaultKernelInstallRoot(const std::string& settingsDirectory) {
    return PathToUtf8(PathFromUtf8(settingsDirectory) / "kernels");
}

KernelInstaller::KernelInstaller(std::unique_ptr<KernelHttpTransport> transport,
                                 std::unique_ptr<KernelArchiveExtractor> extractor,
                                 KernelInstallOptions options)
    : transport_(std::move(transport)),
      extractor_(std::move(extractor)),
      options_(std::move(options)) {}

KernelInstaller::~KernelInstaller() = default;

core::Result<KernelRelease> KernelInstaller::FetchLatest(std::atomic<bool>& cancel,
                                                         const ProgressHandler& progress) {
    Report(progress, InstallStage::Checking, 0, 0, "Querying the latest Mihomo release");
    core::Result<HttpTextResult> response =
        transport_->GetText(kReleaseApi,
                            {{"Accept", "application/vnd.github+json"},
                             {"User-Agent", "ClashYuka/0.2"},
                             {"X-GitHub-Api-Version", "2022-11-28"}},
                            options_.downloadProxy, cancel);
    if (!response) {
        Report(progress, InstallStage::Failed, 0, 0, response.error().message);
        return response.error();
    }
    if (response.value().status == 403) {
        const core::Error error =
            MakeError(ErrorKind::DownloadFailed,
                      "GitHub rejected the release query (HTTP 403). The API may be rate "
                      "limited; try again later.",
                      403);
        Report(progress, InstallStage::Failed, 0, 0, error.message);
        return error;
    }
    if (response.value().status != 200) {
        const core::Error error =
            MakeError(ErrorKind::HttpStatus,
                      "GitHub release query returned HTTP " +
                          std::to_string(response.value().status),
                      response.value().status);
        Report(progress, InstallStage::Failed, 0, 0, error.message);
        return error;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(response.value().body);
    } catch (const std::exception&) {
        const core::Error error =
            MakeError(ErrorKind::InvalidJson, "the GitHub release response is not valid JSON");
        Report(progress, InstallStage::Failed, 0, 0, error.message);
        return error;
    }
    core::Result<KernelRelease> release = ParseKernelRelease(json);
    if (!release) {
        Report(progress, InstallStage::Failed, 0, 0, release.error().message);
        return release;
    }
    if (!IsStableRelease(release.value())) {
        const core::Error error =
            MakeError(ErrorKind::ConfigFailed, "the latest release is a pre-release");
        Report(progress, InstallStage::Failed, 0, 0, error.message);
        return error;
    }
    Report(progress, InstallStage::Checking, 0, 0, "Latest release " + release.value().tag);
    return release;
}

core::Result<KernelInstallResult> KernelInstaller::Install(const KernelRelease& release,
                                                           const KernelAsset& asset,
                                                           std::atomic<bool>& cancel,
                                                           const ProgressHandler& progress) {
    if (asset.name.empty() || asset.url.empty() || asset.size == 0) {
        return MakeError(ErrorKind::DownloadFailed, "the release asset is incomplete");
    }
    if (asset.name.find('/') != std::string::npos ||
        asset.name.find('\\') != std::string::npos || asset.name == "." ||
        asset.name == "..") {
        return MakeError(ErrorKind::DownloadFailed, "the release asset name is unsafe");
    }
    if (options_.installRoot.empty()) {
        return MakeError(ErrorKind::InvalidConfig, "the kernel install directory is empty");
    }

    const fs::path root = PathFromUtf8(options_.installRoot);
    std::error_code createError;
    fs::create_directories(root, createError);
    if (createError) {
        return MakeError(ErrorKind::DownloadFailed,
                         "could not create the kernel install directory: " +
                             createError.message());
    }

    StagingGuard staging{root / (".staging-" + RandomHex(8))};
    fs::create_directories(staging.path, createError);
    if (createError) {
        return MakeError(ErrorKind::DownloadFailed,
                         "could not create the staging directory: " + createError.message());
    }

    const fs::path archive = staging.path / asset.name;
    Report(progress, InstallStage::Downloading, 0, asset.size,
           "Downloading " + asset.name);
    core::Result<void> download = transport_->DownloadFile(
        asset.url, PathToUtf8(archive), options_.downloadProxy,
        [&progress, &asset](std::uint64_t downloaded, std::uint64_t total) {
            Report(progress, InstallStage::Downloading, downloaded,
                   total > 0 ? total : asset.size);
        },
        cancel);
    if (!download) {
        if (download.error().kind == ErrorKind::Cancelled || cancel.load()) {
            Report(progress, InstallStage::Cancelled, 0, asset.size, "Download cancelled");
            return MakeError(ErrorKind::Cancelled, "download cancelled");
        }
        Report(progress, InstallStage::Failed, 0, asset.size, download.error().message);
        return download.error();
    }
    if (cancel.load()) {
        Report(progress, InstallStage::Cancelled, 0, asset.size, "Download cancelled");
        return MakeError(ErrorKind::Cancelled, "download cancelled");
    }

    Report(progress, InstallStage::Verifying, asset.size, asset.size, "Verifying SHA256");
    std::string hashError;
    const std::string actual = Sha256File(PathToUtf8(archive), &hashError);
    if (actual.empty()) {
        const core::Error error =
            MakeError(ErrorKind::DownloadFailed, "could not hash the downloaded archive: " +
                                                     hashError);
        Report(progress, InstallStage::Failed, 0, asset.size, error.message);
        return error;
    }
    const std::string expected = NormalizeSha256(asset.digest);
    bool verified = false;
    if (expected.empty()) {
        if (!options_.allowUnverifiedChecksum) {
            const core::Error error =
                MakeError(ErrorKind::ChecksumMissing,
                          "this release does not provide a verifiable SHA256 digest");
            Report(progress, InstallStage::Failed, 0, asset.size, error.message);
            return error;
        }
        Report(progress, InstallStage::Verifying, asset.size, asset.size,
               "No official checksum; continuing without verification");
    } else {
        if (actual != expected) {
            const core::Error error =
                MakeError(ErrorKind::ChecksumMismatch,
                          "the downloaded archive does not match the release checksum");
            Report(progress, InstallStage::Failed, 0, asset.size, error.message);
            return error;
        }
        verified = true;
    }

    if (!extractor_) {
        return MakeError(ErrorKind::ExtractFailed, "no archive extractor is configured");
    }
    Report(progress, InstallStage::Extracting, asset.size, asset.size,
           "Extracting the Mihomo executable");
    const fs::path executable = staging.path / ExecutableName();
    core::Result<void> extracted =
        extractor_->ExtractExecutable(PathToUtf8(archive), PathToUtf8(executable));
    if (!extracted) {
        Report(progress, InstallStage::Failed, 0, asset.size, extracted.error().message);
        return extracted.error();
    }
    std::error_code sizeError;
    const auto executableSize = fs::file_size(executable, sizeError);
    if (sizeError || executableSize == 0) {
        const core::Error error =
            MakeError(ErrorKind::ExtractFailed, "the archive did not contain a valid executable");
        Report(progress, InstallStage::Failed, 0, asset.size, error.message);
        return error;
    }

    Report(progress, InstallStage::Installing, asset.size, asset.size,
           "Installing kernel " + release.tag);
    std::string versionDirectory = SanitizeVersion(release.tag);
    fs::path target = root / versionDirectory;
    if (fs::exists(target)) {
        target = root / (versionDirectory + "-" + RandomHex(6));
    }
    fs::create_directories(target, createError);
    if (createError) {
        return MakeError(ErrorKind::DownloadFailed,
                         "could not create the version directory: " + createError.message());
    }
    const fs::path targetExecutable = target / ExecutableName();
    fs::rename(executable, targetExecutable, createError);
    if (createError) {
        std::error_code ignored;
        fs::remove_all(target, ignored);
        return MakeError(ErrorKind::DownloadFailed,
                         "could not move the executable into place: " + createError.message());
    }

    nlohmann::json manifest;
    manifest["version"] = release.tag;
    manifest["asset"] = asset.name;
    manifest["sha256"] = actual;
    manifest["checksumVerified"] = verified;
    manifest["installedAt"] = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    std::ofstream manifestFile(target / "install.json", std::ios::binary | std::ios::trunc);
    if (manifestFile) {
        manifestFile << manifest.dump(2) << '\n';
    }

    KernelInstallResult result;
    result.version = release.tag;
    result.executablePath = PathToUtf8(targetExecutable);
    result.assetName = asset.name;
    result.sha256 = actual;
    result.checksumVerified = verified;
    Report(progress, InstallStage::Completed, asset.size, asset.size,
           "Installed " + release.tag);
    return result;
}

}  // namespace clashyuka::runtime
