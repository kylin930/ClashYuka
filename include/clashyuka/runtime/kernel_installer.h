#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "clashyuka/core/result.h"
#include "clashyuka/runtime/kernel_release.h"

namespace clashyuka::runtime {

enum class InstallStage {
    Idle,
    Checking,
    Downloading,
    Verifying,
    Extracting,
    Installing,
    Completed,
    Failed,
    Cancelled,
};

const char* InstallStageName(InstallStage stage);

struct InstallProgress {
    InstallStage stage = InstallStage::Idle;
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    std::string message;
};

struct KernelInstallOptions {
    std::string installRoot;
    // Empty means "use the system proxy".
    std::string downloadProxy;
    // When the release has no digest, the UI must ask the user first.
    bool allowUnverifiedChecksum = false;
};

struct KernelInstallResult {
    std::string version;
    std::string executablePath;
    std::string assetName;
    std::string sha256;
    bool checksumVerified = false;
};

// UI-facing snapshot; immutable and published through AppState.
struct KernelUpdateSnapshot {
    InstallStage stage = InstallStage::Idle;
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    std::string message;
    bool checking = false;
    bool releaseReady = false;
    std::string releaseTag;
    std::string releasePublishedAt;
    std::string releaseUrl;
    std::string assetName;
    std::uint64_t assetSize = 0;
    std::string assetDigest;
    std::string installedVersion;
    std::string installedPath;
    bool installedChecksumVerified = false;
    std::string error;
};

struct HttpTextResult {
    int status = 0;
    std::string body;
};

class KernelHttpTransport {
public:
    using Progress = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;
    virtual ~KernelHttpTransport() = default;
    virtual core::Result<HttpTextResult> GetText(const std::string& url,
                                                 const std::vector<std::pair<std::string, std::string>>& headers,
                                                 const std::string& proxyUrl,
                                                 std::atomic<bool>& cancel) = 0;
    virtual core::Result<void> DownloadFile(const std::string& url, const std::string& targetPath,
                                            const std::string& proxyUrl,
                                            const Progress& progress,
                                            std::atomic<bool>& cancel) = 0;
};

class KernelArchiveExtractor {
public:
    virtual ~KernelArchiveExtractor() = default;
    // Extracts only the Mihomo executable. Implementations must reject path
    // traversal and absolute member names.
    virtual core::Result<void> ExtractExecutable(const std::string& archivePath,
                                                 const std::string& destinationPath) = 0;
};

std::unique_ptr<KernelHttpTransport> CreatePlatformKernelTransport();

std::string DefaultKernelInstallRoot(const std::string& settingsDirectory);

class KernelInstaller final {
public:
    using ProgressHandler = std::function<void(const InstallProgress&)>;

    KernelInstaller(std::unique_ptr<KernelHttpTransport> transport,
                    std::unique_ptr<KernelArchiveExtractor> extractor,
                    KernelInstallOptions options);
    ~KernelInstaller();

    KernelInstaller(const KernelInstaller&) = delete;
    KernelInstaller& operator=(const KernelInstaller&) = delete;

    core::Result<KernelRelease> FetchLatest(std::atomic<bool>& cancel,
                                            const ProgressHandler& progress = {});

    void SetAllowUnverifiedChecksum(bool allow) { options_.allowUnverifiedChecksum = allow; }

    core::Result<KernelInstallResult> Install(const KernelRelease& release,
                                              const KernelAsset& asset,
                                              std::atomic<bool>& cancel,
                                              const ProgressHandler& progress = {});

private:
    std::unique_ptr<KernelHttpTransport> transport_;
    std::unique_ptr<KernelArchiveExtractor> extractor_;
    KernelInstallOptions options_;
};

}  // namespace clashyuka::runtime
