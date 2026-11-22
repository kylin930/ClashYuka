#include "clashyuka/app/archive_extractor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <wx/ffile.h>
#include <wx/log.h>
#include <wx/stream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/zstream.h>

#include "clashyuka/core/result.h"

namespace clashyuka::app {
namespace {

using core::ErrorKind;
using core::MakeError;

constexpr std::uint64_t kMaxExtractedBytes = 256ull * 1024ull * 1024ull;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

bool IsUnsafeMemberName(const wxString& value) {
    wxString name = value;
    name.Replace("\\", "/");
    if (name.empty() || name.StartsWith("/") || name.Contains(":")) {
        return true;
    }
    wxString token;
    while (name.length() > 0) {
        const int slash = name.Find('/');
        token = slash == wxNOT_FOUND ? name : name.Left(slash);
        if (token == "..") {
            return true;
        }
        if (slash == wxNOT_FOUND) {
            break;
        }
        name = name.Mid(slash + 1);
    }
    return false;
}

bool IsExecutableName(const wxString& name) {
#ifdef _WIN32
    const wxString expected = "mihomo.exe";
#else
    const wxString expected = "mihomo";
#endif
    wxString base = name;
    base.Replace("\\", "/");
    const int slash = base.Find('/', true);
    if (slash != wxNOT_FOUND) {
        base = base.Mid(slash + 1);
    }
    return base.Lower() == expected;
}

core::Result<void> CopyStream(wxInputStream& input, const std::filesystem::path& destination) {
    wxFileOutputStream output(destination.wstring());
    if (!output.IsOk()) {
        return MakeError(ErrorKind::ExtractFailed, "could not open the extracted executable");
    }
    std::vector<char> buffer(64 * 1024);
    std::uint64_t total = 0;
    for (;;) {
        input.Read(buffer.data(), static_cast<std::size_t>(buffer.size()));
        const std::size_t read = input.LastRead();
        if (read == 0) {
            break;
        }
        total += read;
        if (total > kMaxExtractedBytes) {
            return MakeError(ErrorKind::ExtractFailed,
                             "the archive expands beyond the allowed size");
        }
        output.Write(buffer.data(), read);
        if (!output.IsOk()) {
            return MakeError(ErrorKind::ExtractFailed, "could not write the extracted executable");
        }
    }
    if (total == 0 || !output.IsOk()) {
        return MakeError(ErrorKind::ExtractFailed, "the archive produced no executable data");
    }
    output.Close();
    return {};
}

class WxArchiveExtractor final : public runtime::KernelArchiveExtractor {
public:
    core::Result<void> ExtractExecutable(const std::string& archivePath,
                                         const std::string& destinationPath) override {
        // Compressed stream errors must become Result failures, not modal
        // wxWidgets log dialogs.
        wxLogNull suppressLogs;
        const std::filesystem::path archive = PathFromUtf8(archivePath);
        const std::filesystem::path destination = PathFromUtf8(destinationPath);
        const std::string extension = Lower(archive.extension().string());

        std::error_code ignored;
        std::filesystem::remove(destination, ignored);

        core::Result<void> result;
        if (extension == ".zip") {
            result = ExtractZip(archive, destination);
        } else if (extension == ".gz") {
            result = ExtractGzip(archive, destination);
        } else {
            return MakeError(ErrorKind::ExtractFailed,
                             "unsupported archive format: " + extension);
        }
        if (!result) {
            std::filesystem::remove(destination, ignored);
            return result;
        }

        const auto size = std::filesystem::file_size(destination, ignored);
        if (ignored || size == 0) {
            std::filesystem::remove(destination, ignored);
            return MakeError(ErrorKind::ExtractFailed, "the extracted executable is empty");
        }
#ifndef _WIN32
        std::filesystem::permissions(
            destination,
            std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add, ignored);
#endif
        return {};
    }

private:
    static core::Result<void> ExtractZip(const std::filesystem::path& archive,
                                         const std::filesystem::path& destination) {
        wxFFileInputStream file(archive.wstring());
        if (!file.IsOk()) {
            return MakeError(ErrorKind::ExtractFailed, "could not open the ZIP archive");
        }
        wxZipInputStream zip(file);
        if (!zip.IsOk()) {
            return MakeError(ErrorKind::ExtractFailed, "the ZIP archive is corrupted");
        }
        std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());
        while (entry) {
            if (IsUnsafeMemberName(entry->GetName())) {
                return MakeError(ErrorKind::ExtractFailed,
                                 "the ZIP archive contains an unsafe member path");
            }
            if (!entry->IsDir() && IsExecutableName(entry->GetName())) {
                return CopyStream(zip, destination);
            }
            entry.reset(zip.GetNextEntry());
        }
        return MakeError(ErrorKind::ExtractFailed,
                         "the ZIP archive does not contain the Mihomo executable");
    }

    static core::Result<void> ExtractGzip(const std::filesystem::path& archive,
                                          const std::filesystem::path& destination) {
        wxFFileInputStream file(archive.wstring());
        if (!file.IsOk()) {
            return MakeError(ErrorKind::ExtractFailed, "could not open the GZIP archive");
        }
        wxZlibInputStream gzip(file, wxZLIB_AUTO);
        if (!gzip.IsOk()) {
            return MakeError(ErrorKind::ExtractFailed, "the GZIP archive is corrupted");
        }
        return CopyStream(gzip, destination);
    }
};

}  // namespace

std::unique_ptr<runtime::KernelArchiveExtractor> CreateWxArchiveExtractor() {
    return std::make_unique<WxArchiveExtractor>();
}

}  // namespace clashyuka::app
