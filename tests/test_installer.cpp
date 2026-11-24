#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <wx/ffile.h>
#include <wx/utils.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/zstream.h>

#include "clashyuka/app/archive_extractor.h"
#include "clashyuka/runtime/kernel_installer.h"
#include "clashyuka/runtime/kernel_release.h"
#include "clashyuka/runtime/sha256.h"

using namespace clashyuka;
using namespace clashyuka::runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("clashyuka-install-" + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string ToUtf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

const char* kReleaseJson = R"JSON({
  "tag_name": "v1.19.34",
  "name": "v1.19.34",
  "published_at": "2026-01-01T00:00:00Z",
  "html_url": "https://github.com/MetaCubeX/mihomo/releases/tag/v1.19.34",
  "prerelease": false,
  "assets": [
    {"name":"mihomo-windows-amd64-compatible-v1.19.34.zip","browser_download_url":"https://example/compatible.zip","size":100,"digest":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    {"name":"mihomo-windows-amd64-v1-v1.19.34.zip","browser_download_url":"https://example/v1.zip","size":100,"digest":"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
    {"name":"mihomo-windows-amd64-v2-v1.19.34.zip","browser_download_url":"https://example/v2.zip","size":100},
    {"name":"mihomo-windows-amd64-v3-v1.19.34.zip","browser_download_url":"https://example/v3.zip","size":100},
    {"name":"mihomo-windows-amd64-v1.19.34.zip","browser_download_url":"https://example/plain.zip","size":100},
    {"name":"mihomo-windows-arm64-v1.19.34.zip","browser_download_url":"https://example/win-arm.zip","size":100},
    {"name":"mihomo-linux-amd64-compatible-v1.19.34.gz","browser_download_url":"https://example/linux.gz","size":100},
    {"name":"mihomo-linux-arm64-v1.19.34.gz","browser_download_url":"https://example/linux-arm.gz","size":100},
    {"name":"mihomo-darwin-amd64-compatible-v1.19.34.gz","browser_download_url":"https://example/mac.gz","size":100},
    {"name":"mihomo-darwin-arm64-v1.19.34.gz","browser_download_url":"https://example/mac-arm.gz","size":100},
    {"name":"Source code (zip)","browser_download_url":"https://example/source.zip","size":100},
    {"name":"mihomo-windows-amd64-v1.19.34-zero.zip","browser_download_url":"https://example/zero.zip","size":0}
  ]
})JSON";

KernelRelease Release() {
    auto parsed = ParseKernelRelease(nlohmann::json::parse(kReleaseJson));
    REQUIRE(parsed.ok());
    return parsed.value();
}

struct FakeTransport final : KernelHttpTransport {
    std::string text = kReleaseJson;
    int status = 200;
    std::string payload = "payload";
    bool cancelDownload = false;
    int downloads = 0;

    core::Result<HttpTextResult> GetText(
        const std::string&, const std::vector<std::pair<std::string, std::string>>&,
        const std::string&, std::atomic<bool>&) override {
        HttpTextResult result;
        result.status = status;
        result.body = text;
        return result;
    }

    core::Result<void> DownloadFile(const std::string&, const std::string& targetPath,
                                    const std::string&, const Progress& progress,
                                    std::atomic<bool>& cancel) override {
        ++downloads;
        if (cancelDownload) {
            cancel.store(true);
            return core::MakeError(core::ErrorKind::Cancelled, "cancelled");
        }
        std::ofstream output(
            fs::path(std::u8string(targetPath.begin(), targetPath.end())),
            std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::MakeError(core::ErrorKind::DownloadFailed, "cannot write");
        }
        output << payload;
        output.close();
        if (progress) {
            progress(payload.size(), payload.size());
        }
        return {};
    }
};

struct FakeExtractor final : KernelArchiveExtractor {
    bool fail = false;
    core::Result<void> ExtractExecutable(const std::string&,
                                         const std::string& destinationPath) override {
        if (fail) {
            return core::MakeError(core::ErrorKind::ExtractFailed, "fake extract failure");
        }
        std::ofstream output(
            fs::path(std::u8string(destinationPath.begin(), destinationPath.end())),
            std::ios::binary | std::ios::trunc);
        output << "executable";
        return {};
    }
};

using ZipEntries = std::vector<std::pair<std::string, std::string>>;

#ifdef _WIN32
constexpr char kLegacyZipExecutable[] = "mihomo.exe";
// Synthetic entry matching the v1.19.34 Windows amd64 v1 release naming.
constexpr char kVersionedZipExecutable[] = "mihomo-windows-amd64-v1-v1.19.34.exe";
constexpr char kCompatibleZipExecutable[] = "mihomo-windows-amd64-compatible.exe";
#else
constexpr char kLegacyZipExecutable[] = "mihomo";
#endif

void WriteZip(const fs::path& path, const ZipEntries& entries, int level = -1) {
    wxFFileOutputStream file(path.wstring());
    REQUIRE(file.IsOk());
    wxZipOutputStream zip(file, level);
    for (const auto& entry : entries) {
        REQUIRE(zip.PutNextEntry(entry.first, wxDateTime::Now(), entry.second.size()));
        zip.Write(entry.second.data(), entry.second.size());
    }
    REQUIRE(zip.Close());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())).good());
}

void WriteGzip(const fs::path& path, const std::string& content) {
    wxFFileOutputStream file(path.wstring());
    REQUIRE(file.IsOk());
    wxZlibOutputStream gzip(file, wxZLIB_GZIP);
    gzip.Write(content.data(), content.size());
    REQUIRE(gzip.Close());
}

}  // namespace

TEST_CASE("SHA256 matches known vectors") {
    CHECK(Sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("release parsing and platform asset selection") {
    const KernelRelease release = Release();
    CHECK(release.tag == "v1.19.34");
    CHECK_FALSE(release.prerelease);
    CHECK(IsStableRelease(release));

    const auto windows = SelectKernelAsset(release, ReleasePlatform::WindowsAmd64);
    REQUIRE(windows.ok());
    CHECK(windows.value().name == "mihomo-windows-amd64-compatible-v1.19.34.zip");

    const auto windowsArm = SelectKernelAsset(release, ReleasePlatform::WindowsArm64);
    REQUIRE(windowsArm.ok());
    CHECK(windowsArm.value().name == "mihomo-windows-arm64-v1.19.34.zip");

    const auto linux = SelectKernelAsset(release, ReleasePlatform::LinuxAmd64);
    REQUIRE(linux.ok());
    CHECK(linux.value().name == "mihomo-linux-amd64-compatible-v1.19.34.gz");

    const auto mac = SelectKernelAsset(release, ReleasePlatform::MacArm64);
    REQUIRE(mac.ok());
    CHECK(mac.value().name == "mihomo-darwin-arm64-v1.19.34.gz");

    CHECK(NormalizeSha256("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ==
          std::string(64, 'a'));
    CHECK(NormalizeSha256("not-a-digest").empty());
}

TEST_CASE("releases without a compatible asset are rejected") {
    auto json = nlohmann::json::parse(kReleaseJson);
    json["assets"] = nlohmann::json::array(
        {{{"name", "mihomo-windows-amd64-v3-v1.19.34.zip"},
          {"browser_download_url", "https://example/v3.zip"},
          {"size", 100}}});
    auto release = ParseKernelRelease(json);
    REQUIRE(release.ok());
    CHECK_FALSE(SelectKernelAsset(release.value(), ReleasePlatform::WindowsAmd64).ok());

    // With v1 and v2 available, v1 wins.
    json["assets"] = nlohmann::json::array(
        {{{"name", "mihomo-windows-amd64-v2-v1.19.34.zip"},
          {"browser_download_url", "https://example/v2.zip"},
          {"size", 100}},
         {{"name", "mihomo-windows-amd64-v1-v1.19.34.zip"},
          {"browser_download_url", "https://example/v1.zip"},
          {"size", 100}}});
    auto v1 = ParseKernelRelease(json);
    REQUIRE(v1.ok());
    auto chosen = SelectKernelAsset(v1.value(), ReleasePlatform::WindowsAmd64);
    REQUIRE(chosen.ok());
    CHECK(chosen.value().name == "mihomo-windows-amd64-v1-v1.19.34.zip");

    json["assets"] = nlohmann::json::array();
    auto empty = ParseKernelRelease(json);
    REQUIRE(empty.ok());
    CHECK_FALSE(SelectKernelAsset(empty.value(), ReleasePlatform::LinuxAmd64).ok());
}

TEST_CASE("release queries surface rate limits and malformed responses") {
    TempDir root;
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* transportPtr = transport.get();
    KernelInstaller installer(std::move(transport), std::make_unique<FakeExtractor>(),
                              KernelInstallOptions{ToUtf8(root.path), "", false});
    std::atomic<bool> cancel{false};

    transportPtr->status = 403;
    auto limited = installer.FetchLatest(cancel);
    REQUIRE_FALSE(limited.ok());
    CHECK(limited.error().kind == core::ErrorKind::DownloadFailed);
    CHECK(limited.error().httpStatus == 403);

    transportPtr->status = 200;
    transportPtr->text = "not json";
    auto malformed = installer.FetchLatest(cancel);
    REQUIRE_FALSE(malformed.ok());
    CHECK(malformed.error().kind == core::ErrorKind::InvalidJson);

    transportPtr->text = kReleaseJson;
    auto ok = installer.FetchLatest(cancel);
    REQUIRE(ok.ok());
    CHECK(ok.value().tag == "v1.19.34");
}

TEST_CASE("installer verifies, installs and keeps older versions") {
    TempDir root;
    auto transport = std::make_unique<FakeTransport>();
    auto* transportPtr = transport.get();
    auto extractor = std::make_unique<FakeExtractor>();
    KernelInstaller installer(
        std::move(transport), std::move(extractor),
        KernelInstallOptions{ToUtf8(root.path), "", false});
    const KernelRelease release = Release();

    KernelAsset asset;
    asset.name = "mihomo-windows-amd64-compatible-v1.19.34.zip";
    asset.url = "https://example/compatible.zip";
    asset.size = 100;
    asset.digest = "sha256:" + Sha256Hex(transportPtr->payload);

    std::atomic<bool> cancel{false};
    auto installed = installer.Install(release, asset, cancel);
    REQUIRE(installed.ok());
    CHECK(installed.value().checksumVerified);
    CHECK(fs::exists(fs::path(std::u8string(installed.value().executablePath.begin(),
                                            installed.value().executablePath.end()))));
    CHECK(fs::exists(root.path / "v1.19.34" / "install.json"));

    // A second install of the same tag must not overwrite the first directory.
    auto second = installer.Install(release, asset, cancel);
    REQUIRE(second.ok());
    CHECK(second.value().executablePath != installed.value().executablePath);
    CHECK(fs::exists(root.path / "v1.19.34" / "install.json"));
}

TEST_CASE("checksum failures and missing digests block installation") {
    TempDir root;
    auto transport = std::make_unique<FakeTransport>();
    auto extractor = std::make_unique<FakeExtractor>();
    KernelInstaller installer(std::move(transport), std::move(extractor),
                              KernelInstallOptions{ToUtf8(root.path), "", false});
    const KernelRelease release = Release();

    KernelAsset asset;
    asset.name = "mihomo-windows-amd64-compatible-v1.19.34.zip";
    asset.url = "https://example/compatible.zip";
    asset.size = 100;
    asset.digest = "sha256:" + std::string(64, '0');

    std::atomic<bool> cancel{false};
    auto mismatch = installer.Install(release, asset, cancel);
    REQUIRE_FALSE(mismatch.ok());
    CHECK(mismatch.error().kind == core::ErrorKind::ChecksumMismatch);

    asset.digest.clear();
    auto missing = installer.Install(release, asset, cancel);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == core::ErrorKind::ChecksumMissing);
    CHECK(fs::is_empty(root.path));

    KernelInstaller permissive(std::make_unique<FakeTransport>(),
                               std::make_unique<FakeExtractor>(),
                               KernelInstallOptions{ToUtf8(root.path), "", true});
    auto accepted = permissive.Install(release, asset, cancel);
    REQUIRE(accepted.ok());
    CHECK_FALSE(accepted.value().checksumVerified);
}

TEST_CASE("cancelled downloads leave no installed kernel") {
    TempDir root;
    auto transport = std::make_unique<FakeTransport>();
    transport->cancelDownload = true;
    auto extractor = std::make_unique<FakeExtractor>();
    KernelInstaller installer(std::move(transport), std::move(extractor),
                              KernelInstallOptions{ToUtf8(root.path), "", false});

    KernelAsset asset;
    asset.name = "mihomo-windows-amd64-compatible-v1.19.34.zip";
    asset.url = "https://example/compatible.zip";
    asset.size = 100;
    asset.digest = "sha256:" + std::string(64, 'a');
    std::atomic<bool> cancel{false};
    auto installed = installer.Install(Release(), asset, cancel);
    REQUIRE_FALSE(installed.ok());
    CHECK(installed.error().kind == core::ErrorKind::Cancelled);
    CHECK(fs::is_empty(root.path));
}

TEST_CASE("real ZIP extraction writes only the executable") {
    TempDir dir;
    const fs::path zip = dir.path / "kernel.zip";
    std::string name = kLegacyZipExecutable;
#ifdef _WIN32
    SUBCASE("legacy basename") { name = "MIHOMO.EXE"; }
    SUBCASE("versioned release basename") { name = kVersionedZipExecutable; }
#endif
    const std::string payload(128 * 1024, 'x');
    WriteZip(zip, {{"folder/" + name, payload}, {"readme.txt", "ignored"}});
    const fs::path destination = dir.path / "mihomo.exe";

    auto extractor = app::CreateWxArchiveExtractor();
    auto result = extractor->ExtractExecutable(ToUtf8(zip), ToUtf8(destination));
    INFO(result.error().message);
    REQUIRE(result.ok());
    CHECK(ReadFile(destination) == std::string(128 * 1024, 'x'));
}

#ifdef _WIN32
TEST_CASE("Windows compatible release ZIP accepts the unversioned member") {
    TempDir dir;
    const auto archive = dir.path / "mihomo-windows-amd64-compatible-v1.19.34.zip";
    const auto destination = dir.path / "extracted.bin";
    const std::string payload(128 * 1024, 'x');
    WriteZip(archive, {{kCompatibleZipExecutable, payload}});

    auto extractor = app::CreateWxArchiveExtractor();
    const auto result = extractor->ExtractExecutable(ToUtf8(archive), ToUtf8(destination));
    INFO(result.error().message);
    REQUIRE(result.ok());
    CHECK(ReadFile(destination) == payload);
}

TEST_CASE("local Mihomo ZIP extraction matches the independently hashed entry") {
    wxString archive;
    if (!wxGetEnv("CLASHYUKA_TEST_MIHOMO_ZIP", &archive)) {
        return;
    }
    wxString expected;
    REQUIRE(wxGetEnv("CLASHYUKA_TEST_MIHOMO_ENTRY_SHA256", &expected));
    const auto expectedHash = NormalizeSha256(expected.utf8_string());
    REQUIRE_FALSE(expectedHash.empty());

    TempDir dir;
    const auto destination = dir.path / "extracted.bin";
    auto extractor = app::CreateWxArchiveExtractor();
    const auto result = extractor->ExtractExecutable(archive.utf8_string(), ToUtf8(destination));
    INFO(result.error().message);
    REQUIRE(result.ok());
    CHECK(Sha256File(ToUtf8(destination)) == expectedHash);
}
#endif

TEST_CASE("real GZIP extraction writes the executable") {
    TempDir dir;
    const fs::path gz = dir.path / "kernel.gz";
    WriteGzip(gz, "gzip-executable");
    const fs::path destination = dir.path / "mihomo";

    auto extractor = app::CreateWxArchiveExtractor();
    auto result = extractor->ExtractExecutable(ToUtf8(gz), ToUtf8(destination));
    INFO(result.error().message);
    REQUIRE(result.ok());
    CHECK(ReadFile(destination) == "gzip-executable");
}

TEST_CASE("ZIP selection is unique and validates every member") {
    TempDir dir;
    auto extractor = app::CreateWxArchiveExtractor();
    const fs::path destination = dir.path / "out.exe";

    for (const auto& [label, entries] : std::vector<std::pair<std::string, ZipEntries>>{
             {"duplicate executable", {{kLegacyZipExecutable, "first"},
#ifdef _WIN32
                                       {kVersionedZipExecutable, "second"}}},
             {"duplicate with unversioned compatible executable",
              {{kVersionedZipExecutable, "first"}, {kCompatibleZipExecutable, "second"}}},
             {"lookalike compatible executable",
              {{"mihomo-windows-amd64-compatible-debug.exe", "other"}}},
#else
                                       {"folder/mihomo", "second"}}},
#endif
             {"unsafe entry after executable", {{kLegacyZipExecutable, "good"},
                                                  {"../evil", "bad"}}},
             {"no matching executable", {{"not-mihomo.exe", "other"}}}}) {
        CAPTURE(label);
        const auto archive = dir.path / (label + ".zip");
        WriteZip(archive, entries);

        auto result = extractor->ExtractExecutable(ToUtf8(archive), ToUtf8(destination));
        CHECK_FALSE(result.ok());
        CHECK_FALSE(fs::exists(destination));
        if (fs::exists(destination)) {
            fs::remove(destination);
        }
    }

    const fs::path corrupt = dir.path / "corrupt.zip";
    WriteBytes(corrupt, "not a zip");
    CHECK_FALSE(extractor->ExtractExecutable(ToUtf8(corrupt), ToUtf8(destination)).ok());

    const fs::path truncated = dir.path / "truncated.zip";
    const fs::path complete = dir.path / "complete.zip";
    WriteZip(complete, {{kLegacyZipExecutable, "truncated-payload"}});
    auto archiveBytes = ReadFile(complete);
    REQUIRE(archiveBytes.size() > 30);
    archiveBytes.resize(archiveBytes.size() - 30);
    WriteBytes(truncated, archiveBytes);
    CHECK_FALSE(extractor->ExtractExecutable(ToUtf8(truncated), ToUtf8(destination)).ok());
}

TEST_CASE("failed real archive install preserves the previous kernel") {
    TempDir root;
    auto transport = std::make_unique<FakeTransport>();
    auto* transportPtr = transport.get();
    KernelInstaller installer(std::move(transport), app::CreateWxArchiveExtractor(),
                              KernelInstallOptions{ToUtf8(root.path), "", false});
    const auto existing = root.path / "v1.19.33" / "mihomo.exe";
    fs::create_directories(existing.parent_path());
    WriteBytes(existing, "previous-kernel");

    KernelAsset asset;
    asset.name = "mihomo-windows-amd64-compatible-v1.19.34.zip";
    asset.url = "https://example/compatible.zip";
    asset.size = 100;
    asset.digest = "sha256:" + Sha256Hex(transportPtr->payload);
    std::atomic<bool> cancel{false};
    const auto result = installer.Install(Release(), asset, cancel);
    REQUIRE_FALSE(result.ok());
    CHECK(ReadFile(existing) == "previous-kernel");
    CHECK(fs::exists(existing));
    CHECK_FALSE(fs::exists(root.path / "v1.19.34"));
}

TEST_CASE("installer supports Unicode install paths") {
    TempDir base;
    const std::string unicode = wxString::FromUTF8("内核 安装 路径").utf8_string();
    const fs::path root =
        base.path / fs::path(std::u8string(unicode.begin(), unicode.end()));
    std::error_code error;
    fs::create_directories(root, error);
    REQUIRE_FALSE(error);

    auto transport = std::make_unique<FakeTransport>();
    auto* transportPtr = transport.get();
    KernelInstaller installer(std::move(transport), std::make_unique<FakeExtractor>(),
                              KernelInstallOptions{ToUtf8(root), "", false});

    KernelAsset asset;
    asset.name = "mihomo-windows-amd64-compatible-v1.19.34.zip";
    asset.url = "https://example/compatible.zip";
    asset.size = 100;
    asset.digest = "sha256:" + Sha256Hex(transportPtr->payload);
    std::atomic<bool> cancel{false};
    auto installed = installer.Install(Release(), asset, cancel);
    REQUIRE(installed.ok());
    CHECK(fs::exists(fs::path(std::u8string(installed.value().executablePath.begin(),
                                            installed.value().executablePath.end()))));
}
