#include "clashyuka/platform/http_download.h"

#include "clashyuka/core/result.h"

namespace clashyuka::platform {
namespace {

core::Result<void> Unsupported() {
    return core::MakeError(core::ErrorKind::DownloadFailed,
                           "HTTPS downloads are not available in this POSIX build");
}

}  // namespace

core::Result<HttpTextResponse> HttpGetText(const std::string&, const HttpHeaders&,
                                           const std::string&, std::atomic<bool>&) {
    return Unsupported();
}

bool HttpsTextAvailable() noexcept { return false; }

core::Result<HttpTextResponse> HttpGetBoundedHttpsText(
    const std::string&, const std::string&, const HttpsTextLimits&, std::atomic<bool>&) {
    return Unsupported();
}

core::Result<void> HttpDownloadFile(const std::string&, const std::string&,
                                    const std::string&, const DownloadProgress&,
                                    std::atomic<bool>&) {
    return Unsupported();
}

}  // namespace clashyuka::platform
