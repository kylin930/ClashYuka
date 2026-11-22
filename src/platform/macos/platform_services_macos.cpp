#include "clashyuka/platform/platform_services.h"

#include <unistd.h>

namespace clashyuka::platform {
namespace {

class MacPlatformServices final : public PlatformServices {
public:
    std::string Name() const override { return "macOS"; }
    bool IsElevated() const override { return ::geteuid() == 0; }
};

}  // namespace

std::unique_ptr<PlatformServices> CreatePlatformServices() {
    return std::make_unique<MacPlatformServices>();
}

}  // namespace clashyuka::platform
