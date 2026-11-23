#pragma once

#include <memory>
#include <string>

#include "clashyuka/core/result.h"

namespace clashyuka::platform {

struct SystemProxyState {
    bool enabled = false;
    std::string server;
    std::string bypass;
};

// Applies the OS HTTP/HTTPS proxy for the Mihomo mixed port. Implementations
// remember the previous OS settings and restore them on Disable/destruction.
class SystemProxyController {
public:
    virtual ~SystemProxyController() = default;

    virtual core::Result<void> Enable(const std::string& host, int port,
                                      const std::string& bypass) = 0;
    virtual core::Result<void> Disable() = 0;
    virtual core::Result<SystemProxyState> Query() = 0;

    // True when this controller has modified the OS settings.
    virtual bool Active() const = 0;
    virtual std::string ActiveServer() const = 0;
};

std::unique_ptr<SystemProxyController> CreateSystemProxyController();

// Platform default bypass list ("<local>" on Windows).
std::string DefaultSystemProxyBypass();

}  // namespace clashyuka::platform
