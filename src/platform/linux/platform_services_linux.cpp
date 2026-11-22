#include "clashyuka/platform/platform_services.h"

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

}  // namespace clashyuka::platform
