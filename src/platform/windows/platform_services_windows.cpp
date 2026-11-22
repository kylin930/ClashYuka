#include "clashyuka/platform/platform_services.h"

#include <windows.h>

namespace clashyuka::platform {
namespace {

class WindowsPlatformServices final : public PlatformServices {
public:
    std::string Name() const override { return "Windows"; }

    bool IsElevated() const override {
        HANDLE token = nullptr;
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
            return false;
        }

        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                             sizeof(elevation), &size);
        ::CloseHandle(token);
        return ok != FALSE && elevation.TokenIsElevated != 0;
    }
};

}  // namespace

std::unique_ptr<PlatformServices> CreatePlatformServices() {
    return std::make_unique<WindowsPlatformServices>();
}

}  // namespace clashyuka::platform
