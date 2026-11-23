#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "clashyuka/core/result.h"

namespace clashyuka::platform {

struct HttpTextResponse {
    int status = 0;
    std::string body;
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;
using DownloadProgress = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;

// Performs a GET request. HTTP error statuses are returned in HttpTextResponse;
// only transport/TLS failures produce an Error.
core::Result<HttpTextResponse> HttpGetText(const std::string& url, const HttpHeaders& headers,
                                           const std::string& proxyUrl,
                                           std::atomic<bool>& cancel);

struct HttpsTextLimits {
    std::size_t maxBytes = 4 * 1024 * 1024;
    std::chrono::milliseconds timeout{30000};  // Whole request, including redirects.
};

bool HttpsTextAvailable() noexcept;
// Verified HTTPS only, including redirects. Non-2xx responses carry no body.
// Empty proxyUrl uses the system proxy. Size errors are InvalidConfig.
core::Result<HttpTextResponse> HttpGetBoundedHttpsText(
    const std::string& url, const std::string& proxyUrl, const HttpsTextLimits& limits,
    std::atomic<bool>& cancel);

// Streams a URL to targetPath. proxyUrl empty means "use the system proxy".
// TLS certificates are always verified; redirects are followed.
core::Result<void> HttpDownloadFile(const std::string& url, const std::string& targetPath,
                                    const std::string& proxyUrl,
                                    const DownloadProgress& onProgress,
                                    std::atomic<bool>& cancel);

}  // namespace clashyuka::platform
