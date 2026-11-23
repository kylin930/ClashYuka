#include "clashyuka/platform/system_proxy.h"

#include "clashyuka/core/result.h"

namespace clashyuka::platform {
namespace {

class UnsupportedSystemProxy final : public SystemProxyController {
public:
    core::Result<void> Enable(const std::string&, int, const std::string&) override {
        return core::MakeError(core::ErrorKind::Internal,
                               "system proxy is not available in this POSIX build");
    }
    core::Result<void> Disable() override { return {}; }
    core::Result<SystemProxyState> Query() override { return SystemProxyState{}; }
    bool Active() const override { return false; }
    std::string ActiveServer() const override { return {}; }
};

}  // namespace

std::unique_ptr<SystemProxyController> CreateSystemProxyController() {
    return std::make_unique<UnsupportedSystemProxy>();
}

std::string DefaultSystemProxyBypass() {
    return "localhost,127.0.0.1,::1";
}

}  // namespace clashyuka::platform
