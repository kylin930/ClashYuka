#include "clashyuka/app/archive_extractor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
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

bool IsUnsafeMemberName(std::string_view name) {
    if (name.empty() || name.front() == '/' || name.front() == '\\' ||
        name.find(':') != name.npos || name.find('\0') != name.npos) {
        return true;
    }
    while (!name.empty()) {
        const auto slash = name.find_first_of("/\\");
        auto component = name.substr(0, slash);
        while (!component.empty() && component.back() == ' ') {
            component.remove_suffix(1);
        }
        if (component == "..") {
            return true;
        }
        if (slash == name.npos) {
            break;
        }
        name.remove_prefix(slash + 1);
    }
    return false;
}

bool IsExecutableName(std::string_view name) {
    const auto base = name.substr(name.find_last_of("/\\") + 1);
#ifdef _WIN32
    if (base.size() > 128) {
        return false;
    }
    // Official Windows archives place the kernel at the archive root using
    // either an unversioned name (e.g. mihomo-windows-amd64-compatible.exe) or
    // a release-versioned name (e.g. mihomo-windows-amd64-v1-v1.19.34.exe).
    // Both forms share the same bounded flavor/go/version grammar so that
    // lookalikes such as ...-compatible-debug.exe are rejected.
    static const std::regex executable(
        R"(mihomo-windows-(?:amd64(?:-compatible|-v[1-3])?|arm64)(?:-go[0-9]+(?:\.[0-9]+)*)?(?:-v[0-9]+\.[0-9]+\.[0-9]+)?\.exe)");
    const auto lower = Lower(std::string(base));
    return lower == "mihomo.exe" || std::regex_match(lower, executable);
#else
    return Lower(std::string(base)) == "mihomo";
#endif
}

std::uint32_t ReadLittleEndian(const unsigned char* bytes, std::size_t length) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
    }
    return value;
}

core::Result<void> InvalidZip() {
    return MakeError(ErrorKind::ExtractFailed, "the ZIP archive is corrupted or truncated");
}

// wxZipEntry strips leading separators. Inspect both on-disk names before wx
// opens the entry, and make sure the local data agrees with the central record.
core::Result<void> CheckZipEntry(wxInputStream& file, wxFileOffset& centralPosition,
                                wxFileOffset centralStart, const wxZipEntry& entry,
                                std::string& name) {
    std::array<unsigned char, 46> central{};
    if (file.SeekI(centralPosition) == wxInvalidOffset ||
        file.Read(central.data(), central.size()).LastRead() != central.size() ||
        ReadLittleEndian(central.data(), 4) != 0x02014b50) {
        return InvalidZip();
    }
    name.resize(ReadLittleEndian(central.data() + 28, 2));
    if (file.Read(name.data(), name.size()).LastRead() != name.size()) {
        return InvalidZip();
    }
    centralPosition += central.size() + name.size() + ReadLittleEndian(central.data() + 30, 2) +
                       ReadLittleEndian(central.data() + 32, 2);
    const auto type = (entry.GetExternalAttributes() >> 16) & 0170000;
    const bool dosReparse = !entry.IsMadeByUnix() &&
                            (entry.GetExternalAttributes() & 0x0400) != 0;
    std::string normalizedName = name;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');
    if (IsUnsafeMemberName(name) || IsUnsafeMemberName(normalizedName) || dosReparse ||
        (entry.IsMadeByUnix() && type != 0 && type != 0100000 && type != 0040000)) {
        return MakeError(ErrorKind::ExtractFailed,
                         "the ZIP archive contains an unsafe member path or type");
    }

    std::array<unsigned char, 30> local{};
    if (entry.GetOffset() < 0 || entry.GetOffset() >= centralStart ||
        file.SeekI(entry.GetOffset()) == wxInvalidOffset ||
        file.Read(local.data(), local.size()).LastRead() != local.size() ||
        ReadLittleEndian(local.data(), 4) != 0x04034b50 ||
        ReadLittleEndian(local.data() + 6, 2) != static_cast<std::uint32_t>(entry.GetFlags()) ||
        ReadLittleEndian(local.data() + 8, 2) != static_cast<std::uint32_t>(entry.GetMethod())) {
        return InvalidZip();
    }
    std::string localName(ReadLittleEndian(local.data() + 26, 2), '\0');
    if (file.Read(localName.data(), localName.size()).LastRead() != localName.size()) {
        return InvalidZip();
    }
    std::string normalizedLocalName = localName;
    std::replace(normalizedLocalName.begin(), normalizedLocalName.end(), '\\', '/');
    if (IsUnsafeMemberName(normalizedLocalName)) {
        return MakeError(ErrorKind::ExtractFailed, "the ZIP archive contains an unsafe member path");
    }
    if (ReadLittleEndian(local.data() + 6, 2) & wxZIP_SUMS_FOLLOW) {
        return MakeError(ErrorKind::ExtractFailed,
                         "the ZIP archive uses unsupported deferred entry metadata");
    }
    const auto dataStart = entry.GetOffset() + static_cast<wxFileOffset>(
        local.size() + localName.size() + ReadLittleEndian(local.data() + 28, 2));
    const auto compressedSize = entry.GetCompressedSize();
    if (localName != name || dataStart > centralStart || compressedSize < 0 ||
        compressedSize > centralStart - dataStart || entry.GetSize() < 0 ||
        (entry.GetFlags() & wxZIP_ENCRYPTED) ||
        (entry.GetMethod() == wxZIP_METHOD_STORE && compressedSize != entry.GetSize())) {
        return InvalidZip();
    }
    if (ReadLittleEndian(local.data() + 14, 4) != entry.GetCrc()) {
        return InvalidZip();
    }
    return {};
}

core::Result<void> ReadData(wxInputStream& input, wxOutputStream* output,
                            std::uint64_t& remaining, wxFileOffset expectedSize = wxInvalidOffset) {
    std::vector<char> buffer(64 * 1024);
    std::uint64_t total = 0;
    while (input.IsOk()) {
        input.Read(buffer.data(), buffer.size());
        const auto read = input.LastRead();
        if (read > remaining) {
            return MakeError(ErrorKind::ExtractFailed, "the archive expands beyond the allowed size");
        }
        remaining -= read;
        total += read;
        if (output && output->Write(buffer.data(), read).LastWrite() != read) {
            return MakeError(ErrorKind::ExtractFailed, "could not write the extracted executable");
        }
        if (read == 0) {
            break;
        }
    }
    if (input.GetLastError() != wxSTREAM_EOF ||
        (expectedSize != wxInvalidOffset && total != static_cast<std::uint64_t>(expectedSize))) {
        return MakeError(ErrorKind::ExtractFailed, "the archive member data is corrupted or truncated");
    }
    return {};
}

core::Result<void> CopyStream(wxInputStream& input, const std::filesystem::path& destination,
                              std::uint64_t& remaining, wxFileOffset size = wxInvalidOffset) {
    wxFileOutputStream output(destination.wstring());
    if (!output.IsOk()) {
        return MakeError(ErrorKind::ExtractFailed, "could not open the extracted executable");
    }
    const auto initialRemaining = remaining;
    auto result = ReadData(input, &output, remaining, size);
    if (!result) {
        return result;
    }
    if (remaining == initialRemaining) {
        return MakeError(ErrorKind::ExtractFailed, "the archive produced no executable data");
    }
    if (!output.Close()) {
        return MakeError(ErrorKind::ExtractFailed, "could not close the extracted executable");
    }
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
        wxFFileInputStream headers(archive.wstring());
        wxZipInputStream zip(file);
        const int expectedEntries = zip.GetTotalEntries();
        // On a seekable stream wx positions the file just past the first
        // central directory signature when loading the entry count.
        const auto centralStart = file.TellI() - 4;
        if (!headers.IsOk() || !zip.IsOk() || centralStart < 0 || expectedEntries < 0) {
            return InvalidZip();
        }

        auto centralPosition = centralStart;
        int entries = 0;
        std::unique_ptr<wxZipEntry> executable;
        auto remaining = kMaxExtractedBytes;
        while (auto entry = std::unique_ptr<wxZipEntry>(zip.GetNextEntry())) {
            if (++entries > expectedEntries) {
                return InvalidZip();
            }
            std::string name;
            auto checked = CheckZipEntry(headers, centralPosition, centralStart, *entry, name);
            if (!checked) {
                return checked;
            }
            const bool isDirectory = entry->IsDir() || name.back() == '/' || name.back() == '\\' ||
                                     (entry->IsMadeByUnix() &&
                                      ((entry->GetExternalAttributes() >> 16) & 0170000) == 0040000);
            const bool isExecutable = !isDirectory && IsExecutableName(name);
            if (isExecutable && executable) {
                return MakeError(ErrorKind::ExtractFailed,
                                 "the ZIP archive contains multiple Mihomo executables");
            }
            if (isExecutable) {
                executable = std::move(entry);
            }
        }
        if (zip.GetLastError() != wxSTREAM_EOF || entries != expectedEntries) {
            return InvalidZip();
        }
        if (!executable) {
            return MakeError(ErrorKind::ExtractFailed,
                             "the ZIP archive does not contain the Mihomo executable");
        }
        if (executable->GetSize() <= 0) {
            return MakeError(ErrorKind::ExtractFailed,
                             "the ZIP archive contains an empty Mihomo executable");
        }
        if (!zip.OpenEntry(*executable)) {
            return InvalidZip();
        }
        remaining = kMaxExtractedBytes;
        return CopyStream(zip, destination, remaining, executable->GetSize());
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
        auto remaining = kMaxExtractedBytes;
        return CopyStream(gzip, destination, remaining);
    }
};

}  // namespace

std::unique_ptr<runtime::KernelArchiveExtractor> CreateWxArchiveExtractor() {
    return std::make_unique<WxArchiveExtractor>();
}

}  // namespace clashyuka::app
