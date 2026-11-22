#include "clashyuka/platform/platform_services.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <unistd.h>

namespace clashyuka::platform {
namespace {

class LinuxPlatformServices final : public PlatformServices {
public:
    std::string Name() const override { return "Linux"; }
    bool IsElevated() const override { return ::geteuid() == 0; }
};

}  // namespace

std::unique_ptr<PlatformServices> CreatePlatformServices() {
    return std::make_unique<LinuxPlatformServices>();
}

bool ReplaceFileAtomically(const std::string& tempPath, const std::string& targetPath,
                           std::string* error) {
    const std::filesystem::path temp(std::u8string(tempPath.begin(), tempPath.end()));
    const std::filesystem::path target(std::u8string(targetPath.begin(), targetPath.end()));
    if (std::rename(temp.c_str(), target.c_str()) != 0) {
        if (error != nullptr) {
            *error = "rename failed: " + std::string(std::strerror(errno));
        }
        return false;
    }
    return true;
}

}  // namespace clashyuka::platform
