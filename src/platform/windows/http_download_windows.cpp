#include "clashyuka/platform/http_download.h"

#define NOMINMAX
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "http_download_windows.h"

#include "clashyuka/core/result.h"

namespace clashyuka::platform {
namespace {

using core::ErrorKind;
using core::MakeError;

constexpr wchar_t kUserAgent[] = L"ClashYuka/0.2 (Windows)";
constexpr int kMaxRedirects = 5;
constexpr std::uint64_t kMaxTextBytes = 8 * 1024 * 1024;

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          wide.data(), length);
    return wide;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0,
                                             nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          utf8.data(), length, nullptr, nullptr);
    return utf8;
}

struct UrlParts {
    std::wstring scheme;
    std::wstring host;
    std::wstring target;
    INTERNET_PORT port = 0;
    bool secure = false;
};

bool CrackUrl(const std::string& url, UrlParts& parts, bool allowEmptyTarget = false) {
    const std::wstring wide = ToWide(url);
    if (wide.empty()) {
        return false;
    }
    wchar_t scheme[16]{};
    wchar_t host[256]{};
    wchar_t path[4096]{};
    wchar_t extra[2048]{};
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.lpszScheme = scheme;
    components.dwSchemeLength = 16;
    components.lpszHostName = host;
    components.dwHostNameLength = 256;
    components.lpszUrlPath = path;
    components.dwUrlPathLength = 4096;
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = 2048;
    if (::WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &components) ==
        FALSE) {
        return false;
    }
    parts.scheme.assign(scheme, components.dwSchemeLength);
    parts.host.assign(host, components.dwHostNameLength);
    parts.target.assign(path, components.dwUrlPathLength);
    parts.target.append(extra, components.dwExtraInfoLength);
    parts.port = components.nPort;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !parts.host.empty() && (allowEmptyTarget || !parts.target.empty());
}

std::string ResolveRedirect(const UrlParts& base, const std::wstring& location) {
    if (location.rfind(L"http://", 0) == 0 || location.rfind(L"https://", 0) == 0) {
        return ToUtf8(location);
    }
    std::wstring url = base.scheme + L"://" + base.host;
    url += L":" + std::to_wstring(base.port);
    if (location.empty() || location.front() != L'/') {
        const std::size_t slash = base.target.rfind(L'/');
        url += slash == std::wstring::npos ? L"/" : base.target.substr(0, slash + 1);
    }
    url += location;
    return ToUtf8(url);
}

struct WinHttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    ~WinHttpHandles() {
        if (request != nullptr) {
            ::WinHttpCloseHandle(request);
        }
        if (connect != nullptr) {
            ::WinHttpCloseHandle(connect);
        }
        if (session != nullptr) {
            ::WinHttpCloseHandle(session);
        }
    }
};

HINTERNET OpenSession(const std::string& proxyUrl, DWORD flags = 0) {
    if (!proxyUrl.empty()) {
        UrlParts proxy;
        if (!CrackUrl(proxyUrl, proxy, (flags & WINHTTP_FLAG_ASYNC) != 0)) {
            return nullptr;
        }
        const std::wstring proxySpec =
            proxy.host + L":" + std::to_wstring(proxy.port);
        return ::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                             proxySpec.c_str(), WINHTTP_NO_PROXY_BYPASS, flags);
    }
    HINTERNET session = ::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, flags);
    if (session == nullptr) {
        session = ::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, flags);
    }
    return session;
}

bool AddHeaders(HINTERNET request, const HttpHeaders& headers) {
    for (const auto& header : headers) {
        const std::wstring line = ToWide(header.first + ": " + header.second);
        if (line.empty()) {
            continue;
        }
        if (::WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(line.size()),
                                       WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ==
            FALSE) {
            return false;
        }
    }
    return true;
}

bool QueryStatus(HINTERNET request, DWORD& status) {
    DWORD size = sizeof(status);
    return ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                 WINHTTP_NO_HEADER_INDEX) != FALSE;
}

std::wstring QueryLocation(HINTERNET request) {
    DWORD size = 0;
    ::WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                          WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (size == 0) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                              value.data(), &size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::uint64_t QueryContentLength(HINTERNET request) {
    wchar_t value[64]{};
    DWORD size = sizeof(value);
    if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                              WINHTTP_HEADER_NAME_BY_INDEX, value, &size,
                              WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return 0;
    }
    return std::strtoull(ToUtf8(value).c_str(), nullptr, 10);
}

bool IsRedirect(DWORD status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// One request without following redirects. Writes the body through `sink`.
template <typename Sink>
core::Result<DWORD> RequestOnce(const std::string& url, const HttpHeaders& headers,
                                const std::string& proxyUrl, std::atomic<bool>& cancel,
                                Sink sink, std::uint64_t& total, std::string& location,
                                bool returnHttpErrors) {
    UrlParts parts;
    if (!CrackUrl(url, parts)) {
        return MakeError(ErrorKind::InvalidConfig, "invalid download URL");
    }
    WinHttpHandles handles;
    handles.session = OpenSession(proxyUrl);
    if (handles.session == nullptr) {
        return MakeError(ErrorKind::DownloadFailed, "could not open an HTTP session");
    }
    ::WinHttpSetTimeouts(handles.session, 10000, 10000, 10000, 30000);
    handles.connect = ::WinHttpConnect(handles.session, parts.host.c_str(), parts.port, 0);
    if (handles.connect == nullptr) {
        return MakeError(ErrorKind::DownloadFailed, "could not connect to the download host");
    }
    handles.request = ::WinHttpOpenRequest(
        handles.connect, L"GET", parts.target.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (handles.request == nullptr) {
        return MakeError(ErrorKind::DownloadFailed, "could not create the HTTP request");
    }
    if (!AddHeaders(handles.request, headers)) {
        return MakeError(ErrorKind::DownloadFailed, "could not add HTTP request headers");
    }
    if (cancel.load()) {
        return MakeError(ErrorKind::Cancelled, "download cancelled");
    }
    if (::WinHttpSendRequest(handles.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
        ::WinHttpReceiveResponse(handles.request, nullptr) == FALSE) {
        return MakeError(ErrorKind::DownloadFailed, "the download request failed");
    }

    DWORD status = 0;
    if (!QueryStatus(handles.request, status)) {
        return MakeError(ErrorKind::DownloadFailed, "could not read the HTTP status");
    }
    total = QueryContentLength(handles.request);
    if (IsRedirect(status)) {
        location = ToUtf8(QueryLocation(handles.request));
        return status;
    }
    if (status < 200 || status >= 300) {
        if (!returnHttpErrors) {
            return MakeError(ErrorKind::HttpStatus,
                             "the download server returned HTTP " + std::to_string(status),
                             static_cast<int>(status));
        }
    }

    std::uint64_t downloaded = 0;
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        if (cancel.load()) {
            return MakeError(ErrorKind::Cancelled, "download cancelled");
        }
        DWORD available = 0;
        if (::WinHttpQueryDataAvailable(handles.request, &available) == FALSE) {
            return MakeError(ErrorKind::DownloadFailed, "the download stream failed");
        }
        if (available == 0) {
            break;
        }
        const DWORD chunk = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        DWORD read = 0;
        if (::WinHttpReadData(handles.request, buffer.data(), chunk, &read) == FALSE) {
            return MakeError(ErrorKind::DownloadFailed, "could not read the download stream");
        }
        if (read == 0) {
            break;
        }
        if (!sink(buffer.data(), static_cast<std::size_t>(read))) {
            return MakeError(ErrorKind::DownloadFailed, "could not write the downloaded data");
        }
        downloaded += read;
    }
    return status;
}

// Each async WinHTTP operation is awaited on the calling worker. Closing the
// request interrupts I/O; HANDLE_CLOSING is the lifetime fence for the callback
// context and read buffer, including after timeout or cancellation.
class BoundedRequest {
public:
    using Clock = std::chrono::steady_clock;

    explicit BoundedRequest(WinHttpHandles& handles) : handles_(handles) {}
    ~BoundedRequest() {
        if (registered_) {
            ::WinHttpCloseHandle(std::exchange(handles_.request, nullptr));
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return closed_; });
        }
    }

    bool Register() {
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        if (!::WinHttpSetOption(handles_.request, WINHTTP_OPTION_CONTEXT_VALUE,
                                &context, sizeof(context))) {
            return false;
        }
        registered_ = ::WinHttpSetStatusCallback(
            handles_.request, OnStatus,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
            0) != WINHTTP_INVALID_STATUS_CALLBACK;
        return registered_;
    }

    template <typename Start>
    core::Result<DWORD> Run(Start start, Clock::time_point deadline, std::atomic<bool>& cancel) {
        if (auto stopped = StopReason(deadline, cancel); stopped) {
            return stopped;
        }
        {
            std::lock_guard lock(mutex_);
            ready_ = false;
        }
        if (!start() && ::GetLastError() != ERROR_IO_PENDING) {
            return IoError(::GetLastError());
        }
        std::unique_lock lock(mutex_);
        while (!ready_) {
            if (auto stopped = StopReason(deadline, cancel); stopped) {
                return stopped;
            }
            changed_.wait_until(lock, std::min(deadline, Clock::now() +
                                              std::chrono::milliseconds(20)));
        }
        if (auto stopped = StopReason(deadline, cancel); stopped) {
            return stopped;
        }
        return error_ == ERROR_SUCCESS ? core::Result<DWORD>(bytes_) : IoError(error_);
    }

    DWORD_PTR Context() { return reinterpret_cast<DWORD_PTR>(this); }
    char* Buffer() { return buffer_; }
    DWORD Capacity() const { return sizeof(buffer_); }

private:
    static core::Error StopReason(Clock::time_point deadline, std::atomic<bool>& cancel) {
        if (cancel.load()) {
            return MakeError(ErrorKind::Cancelled, "download cancelled");
        }
        if (Clock::now() >= deadline) {
            return MakeError(ErrorKind::Timeout, "HTTPS download timed out");
        }
        return {};
    }

    static core::Error IoError(DWORD error) {
        if (error == ERROR_WINHTTP_TIMEOUT) {
            return MakeError(ErrorKind::Timeout, "HTTPS download timed out");
        }
        if (error == ERROR_WINHTTP_SECURE_FAILURE || error == ERROR_WINHTTP_SECURE_INVALID_CA) {
            return MakeError(ErrorKind::DownloadFailed, "HTTPS server certificate validation failed");
        }
        return MakeError(ErrorKind::DownloadFailed, "the HTTPS download request failed");
    }

    static void CALLBACK OnStatus(HINTERNET, DWORD_PTR context, DWORD status,
                                  LPVOID info, DWORD length) {
        if (context == 0) {
            return;
        }
        auto& self = *reinterpret_cast<BoundedRequest*>(context);
        std::lock_guard lock(self.mutex_);
        switch (status) {
            case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
                self.closed_ = true;
                break;
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
                self.error_ = static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError;
                self.ready_ = true;
                break;
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
                self.error_ = ERROR_SUCCESS;
                self.bytes_ = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
                self.ready_ = true;
                break;
            default:
                return;
        }
        self.changed_.notify_one();
    }

    WinHttpHandles& handles_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool registered_ = false;
    bool closed_ = false;
    bool ready_ = false;
    DWORD error_ = ERROR_SUCCESS;
    DWORD bytes_ = 0;
    char buffer_[16 * 1024];
};

}  // namespace

bool detail::ConfigureBoundedHttpsRequest(HINTERNET request, bool followRedirects) {
    DWORD policy = followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP
                                   : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD redirects = kMaxRedirects;
    DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    return ::WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) &&
           ::WinHttpSetOption(request, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                              &redirects, sizeof(redirects)) &&
           ::WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
}

bool HttpsTextAvailable() noexcept { return true; }

core::Result<HttpTextResponse> HttpGetBoundedHttpsText(
    const std::string& url, const std::string& proxyUrl, const HttpsTextLimits& limits,
    std::atomic<bool>& cancel) {
    if (cancel.load()) {
        return MakeError(ErrorKind::Cancelled, "download cancelled");
    }
    if (limits.timeout <= std::chrono::milliseconds::zero()) {
        return MakeError(ErrorKind::Timeout, "HTTPS download timed out");
    }
    const auto deadline = BoundedRequest::Clock::now() +
                          std::min(limits.timeout, std::chrono::milliseconds(300000));
    UrlParts parts;
    const auto scheme = url.find("://");
    const auto authority = scheme == url.npos ? std::string_view{} :
                           std::string_view(url).substr(scheme + 3);
    const auto suffix = authority.find_first_of("/?#");
    if (url.size() > 8192 || authority.substr(0, suffix).find('@') != url.npos ||
        std::ranges::any_of(url, [](unsigned char ch) { return ch <= 32 || ch == 127 || ch == '\\'; }) ||
        !CrackUrl(url.substr(0, url.find('#')), parts, true) || !parts.secure) {
        return MakeError(ErrorKind::InvalidConfig, "a valid HTTPS download URL is required");
    }
    if (parts.target.empty() || parts.target.front() == L'?') {
        parts.target.insert(0, 1, L'/');
    }

    WinHttpHandles handles;
    handles.session = OpenSession(proxyUrl, WINHTTP_FLAG_ASYNC);
    if (handles.session) {
        handles.connect = ::WinHttpConnect(handles.session, parts.host.c_str(), parts.port, 0);
    }
    if (handles.connect) {
        handles.request = ::WinHttpOpenRequest(
            handles.connect, L"GET", parts.target.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (!handles.request) {
        return MakeError(ErrorKind::DownloadFailed, "could not open an HTTPS request");
    }
    if (!detail::ConfigureBoundedHttpsRequest(handles.request, limits.followRedirects)) {
        return MakeError(ErrorKind::DownloadFailed, "could not configure the HTTPS request");
    }
    BoundedRequest pending(handles);
    if (!pending.Register()) {
        return MakeError(ErrorKind::DownloadFailed, "could not configure HTTPS cancellation");
    }
    auto result = pending.Run([&] {
        return ::WinHttpSendRequest(handles.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, pending.Context());
    }, deadline, cancel);
    if (!result) {
        return result.error();
    }
    result = pending.Run([&] { return ::WinHttpReceiveResponse(handles.request, nullptr); },
                         deadline, cancel);
    if (!result) {
        return result.error();
    }

    DWORD status = 0;
    if (!QueryStatus(handles.request, status)) {
        return MakeError(ErrorKind::DownloadFailed, "could not read the HTTPS status");
    }
    HttpTextResponse response{static_cast<int>(status), {}};
    if (status < 200 || status >= 300) {
        return response;
    }
    const auto tooLarge = MakeError(ErrorKind::InvalidConfig, "HTTPS response exceeds the size limit");
    if (QueryContentLength(handles.request) > limits.maxBytes) {
        return tooLarge;
    }
    for (;;) {
        result = pending.Run([&] {
            return ::WinHttpReadData(handles.request, pending.Buffer(), pending.Capacity(), nullptr);
        }, deadline, cancel);
        if (!result) {
            return result.error();
        }
        const auto bytes = result.value();
        if (bytes == 0) {
            return response;
        }
        if (bytes > limits.maxBytes - response.body.size()) {
            return tooLarge;
        }
        response.body.append(pending.Buffer(), bytes);
    }
}

core::Result<HttpTextResponse> HttpGetText(const std::string& url, const HttpHeaders& headers,
                                           const std::string& proxyUrl,
                                           std::atomic<bool>& cancel) {
    std::string current = url;
    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        HttpTextResponse response;
        std::string body;
        std::uint64_t total = 0;
        std::string location;
        auto result = RequestOnce(
            current, headers, proxyUrl, cancel,
            [&body](const char* data, std::size_t size) {
                if (body.size() + size > kMaxTextBytes) {
                    return false;
                }
                body.append(data, size);
                return true;
            },
            total, location, true);
        if (!result) {
            return result.error();
        }
        response.status = static_cast<int>(result.value());
        if (IsRedirect(result.value())) {
            if (location.empty()) {
                return MakeError(ErrorKind::DownloadFailed, "redirect without a location");
            }
            UrlParts base;
            CrackUrl(current, base);
            current = ResolveRedirect(base, ToWide(location));
            continue;
        }
        response.body = std::move(body);
        return response;
    }
    return MakeError(ErrorKind::DownloadFailed, "too many HTTP redirects");
}

core::Result<void> HttpDownloadFile(const std::string& url, const std::string& targetPath,
                                    const std::string& proxyUrl,
                                    const DownloadProgress& onProgress,
                                    std::atomic<bool>& cancel) {
    std::string current = url;
    std::error_code ignored;
    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        const std::filesystem::path file(std::u8string(targetPath.begin(), targetPath.end()));
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output) {
            return MakeError(ErrorKind::DownloadFailed, "could not open the download target");
        }
        std::uint64_t total = 0;
        std::uint64_t downloaded = 0;
        std::string location;
        auto result = RequestOnce(
            current, {{"User-Agent", "ClashYuka/0.2"}}, proxyUrl, cancel,
            [&output, &downloaded, &onProgress, &total](const char* data, std::size_t size) {
                output.write(data, static_cast<std::streamsize>(size));
                if (!output) {
                    return false;
                }
                downloaded += size;
                if (onProgress) {
                    onProgress(downloaded, total);
                }
                return true;
            },
            total, location, false);
        output.close();
        if (!result) {
            std::filesystem::remove(file, ignored);
            return result.error();
        }
        if (IsRedirect(result.value())) {
            std::filesystem::remove(file, ignored);
            if (location.empty()) {
                return MakeError(ErrorKind::DownloadFailed, "redirect without a location");
            }
            UrlParts base;
            CrackUrl(current, base);
            current = ResolveRedirect(base, ToWide(location));
            continue;
        }
        return {};
    }
    return MakeError(ErrorKind::DownloadFailed, "too many HTTP redirects");
}

}  // namespace clashyuka::platform
