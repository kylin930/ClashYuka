#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include "clashyuka/platform/http_download.h"
#include "mock/mock_server.h"

#ifdef _WIN32
#define SECURITY_WIN32
#include <objbase.h>
#include <schannel.h>
#include <security.h>
#include <wincrypt.h>

#include "platform/windows/http_download_windows.h"
#endif

using namespace clashyuka;

namespace {

struct TempFile {
    std::filesystem::path path;
    TempFile() {
        path = std::filesystem::temp_directory_path() /
               ("clashyuka-http-" + std::to_string(std::rand()));
    }
    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

#ifdef _WIN32
struct WinHttpHandle {
    HINTERNET value;
    ~WinHttpHandle() {
        if (value) {
            ::WinHttpCloseHandle(value);
        }
    }
};

// A loopback-only TLS peer. It accepts CONNECT locally but never forwards traffic.
// The self-signed certificate is never installed or added to a trust store.
class UntrustedTlsPeer : public httplib::Server {
public:
    ~UntrustedTlsPeer() override {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        if (SecIsValidHandle(&credential_)) {
            ::FreeCredentialsHandle(&credential_);
        }
        if (certificate_) {
            ::CertFreeCertificateContext(certificate_);
        }
        if (key_) {
            ::CryptDestroyKey(key_);
        }
        if (provider_) {
            ::CryptReleaseContext(provider_, 0);
            HCRYPTPROV ignored;
            ::CryptAcquireContextW(&ignored, keyName_, MS_ENH_RSA_AES_PROV_W,
                                   PROV_RSA_AES, CRYPT_DELETEKEYSET | CRYPT_SILENT);
        }
    }

    bool PrepareCertificate() {
        GUID keyId{};
        if (FAILED(::CoCreateGuid(&keyId)) || !::StringFromGUID2(keyId, keyName_, 39) ||
            !::CryptAcquireContextW(&provider_, keyName_, MS_ENH_RSA_AES_PROV_W,
                                    PROV_RSA_AES, CRYPT_NEWKEYSET | CRYPT_SILENT) ||
            !::CryptGenKey(provider_, AT_KEYEXCHANGE, 2048u << 16, &key_)) {
            return false;
        }
        BYTE subjectBytes[128];
        CERT_NAME_BLOB subject{sizeof(subjectBytes), subjectBytes};
        if (!::CertStrToNameW(X509_ASN_ENCODING, L"CN=tls-fixture.invalid", CERT_X500_NAME_STR,
                              nullptr, subject.pbData, &subject.cbData, nullptr)) {
            return false;
        }
        CRYPT_ALGORITHM_IDENTIFIER signature{const_cast<char*>(szOID_RSA_SHA256RSA), {}};
        CRYPT_KEY_PROV_INFO keyInfo{};
        keyInfo.pwszContainerName = keyName_;
        keyInfo.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        keyInfo.dwProvType = PROV_RSA_AES;
        keyInfo.dwKeySpec = AT_KEYEXCHANGE;
        certificate_ = ::CertCreateSelfSignCertificate(provider_, &subject, 0, &keyInfo,
            &signature, nullptr, nullptr, nullptr);
        return certificate_ != nullptr;
    }

    SECURITY_STATUS AcquireCredential() {
        SCHANNEL_CRED settings{};
        settings.dwVersion = SCHANNEL_CRED_VERSION;
        settings.cCreds = 1;
        settings.paCred = &certificate_;
        settings.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
        TimeStamp expiry{};
        return ::AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W),
            SECPKG_CRED_INBOUND, nullptr, &settings, nullptr, nullptr, &credential_, &expiry);
    }

    int Start() {
        const int port = bind_to_any_port("127.0.0.1");
        if (port > 0) {
            thread_ = std::thread([this] { listen_after_bind(); });
            wait_until_ready();
        }
        return port;
    }

    bool SentServerFlight() const { return sentServerFlight_; }

private:
    bool process_and_close_socket(socket_t socket) override {
        httplib::detail::SocketStream stream(socket, 2, 0, 2, 0);
        const bool sent = SendCertificate(stream);
        ::closesocket(socket);
        return sent;
    }

    bool SendCertificate(httplib::Stream& stream) {
        std::string connect;
        for (char ch; connect.size() < 4096 && !connect.ends_with("\r\n\r\n");) {
            if (stream.read(&ch, 1) != 1) {
                return false;
            }
            connect += ch;
        }
        constexpr std::string_view connected = "HTTP/1.1 200 Connection Established\r\n\r\n";
        if (!connect.starts_with("CONNECT tls-fixture.invalid:443 ") ||
            !connect.ends_with("\r\n\r\n") || !WriteAll(stream, connected)) {
            return false;
        }

        CtxtHandle context;
        SecInvalidateHandle(&context);
        std::string pending;
        for (int record = 0; record < 12; ++record) {
            unsigned char header[5];
            if (!ReadAll(stream, reinterpret_cast<char*>(header), sizeof(header))) {
                break;
            }
            const unsigned length = (header[3] << 8) | header[4];
            const auto offset = pending.size() + sizeof(header);
            pending.append(reinterpret_cast<const char*>(header), sizeof(header));
            pending.resize(offset + length);
            if (!ReadAll(stream, pending.data() + offset, length)) {
                break;
            }
            SecBuffer inputBuffers[] = {{static_cast<unsigned long>(pending.size()),
                                         SECBUFFER_TOKEN, pending.data()},
                                        {0, SECBUFFER_EMPTY, nullptr}};
            SecBuffer outputBuffer{0, SECBUFFER_TOKEN, nullptr};
            SecBufferDesc input{SECBUFFER_VERSION, 2, inputBuffers};
            SecBufferDesc output{SECBUFFER_VERSION, 1, &outputBuffer};
            unsigned long attributes = 0;
            TimeStamp expiry{};
            const auto status = ::AcceptSecurityContext(&credential_,
                SecIsValidHandle(&context) ? &context : nullptr, &input,
                ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM, SECURITY_NATIVE_DREP, &context, &output,
                &attributes, &expiry);
            const auto* token = static_cast<const char*>(outputBuffer.pvBuffer);
            const bool written = !token || WriteAll(stream, {token, outputBuffer.cbBuffer});
            if (token) {
                ::FreeContextBuffer(outputBuffer.pvBuffer);
            }
            if (!written || (status != SEC_E_INCOMPLETE_MESSAGE &&
                             status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)) {
                break;
            }
            if (status == SEC_I_CONTINUE_NEEDED) {
                pending.clear();
                sentServerFlight_ = true;
            } else if (status == SEC_E_OK) {
                // Keep the connection open while WinHTTP validates the peer.
                char reply[256];
                stream.read(reply, sizeof(reply));
                break;
            }
        }
        if (SecIsValidHandle(&context)) {
            ::DeleteSecurityContext(&context);
        }
        return sentServerFlight_;
    }

    static bool ReadAll(httplib::Stream& stream, char* data, std::size_t length) {
        while (length > 0) {
            const auto count = stream.read(data, length);
            if (count <= 0) {
                return false;
            }
            data += count;
            length -= count;
        }
        return true;
    }

    static bool WriteAll(httplib::Stream& stream, std::string_view data) {
        while (!data.empty()) {
            const auto count = stream.write(data.data(), data.size());
            if (count <= 0) {
                return false;
            }
            data.remove_prefix(count);
        }
        return true;
    }

    wchar_t keyName_[39]{};
    HCRYPTPROV provider_ = 0;
    HCRYPTKEY key_ = 0;
    PCCERT_CONTEXT certificate_ = nullptr;
    CredHandle credential_{static_cast<ULONG_PTR>(-1), static_cast<ULONG_PTR>(-1)};
    std::atomic<bool> sentServerFlight_{false};
    std::thread thread_;
};
#endif

}  // namespace

TEST_CASE("platform HTTP download reads text, follows redirects and streams files") {
    MockServer server;
    auto& srv = server.server();
    srv.Get("/text", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("hello", "text/plain");
    });
    srv.Get("/redirect", [](const httplib::Request&, httplib::Response& response) {
        response.status = 302;
        response.set_header("Location", "/text");
    });
    srv.Get("/file", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("archive-payload", "application/octet-stream");
    });
    srv.Get("/missing", [](const httplib::Request&, httplib::Response& response) {
        response.status = 404;
        response.set_content("not found", "text/plain");
    });
    REQUIRE(server.Start());

    std::atomic<bool> cancel{false};
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());

    auto text = platform::HttpGetText(base + "/text", {}, "", cancel);
    REQUIRE(text.ok());
    CHECK(text.value().status == 200);
    CHECK(text.value().body == "hello");

    auto redirected = platform::HttpGetText(base + "/redirect", {}, "", cancel);
    REQUIRE(redirected.ok());
    CHECK(redirected.value().status == 200);
    CHECK(redirected.value().body == "hello");

    auto missing = platform::HttpGetText(base + "/missing", {}, "", cancel);
    REQUIRE(missing.ok());
    CHECK(missing.value().status == 404);

    TempFile file;
    double lastProgress = 0.0;
    auto downloaded = platform::HttpDownloadFile(
        base + "/file", file.path.string(), "",
        [&lastProgress](std::uint64_t downloaded, std::uint64_t total) {
            if (total > 0) {
                lastProgress = static_cast<double>(downloaded) / static_cast<double>(total);
            }
        },
        cancel);
    REQUIRE(downloaded.ok());
    CHECK(lastProgress == doctest::Approx(1.0));
    std::ifstream input(file.path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content == "archive-payload");

    auto failedDownload =
        platform::HttpDownloadFile(base + "/missing", file.path.string(), "", {}, cancel);
    REQUIRE_FALSE(failedDownload.ok());
    CHECK(failedDownload.error().kind == core::ErrorKind::HttpStatus);

    cancel.store(true);
    auto cancelled = platform::HttpGetText(base + "/text", {}, "", cancel);
    REQUIRE_FALSE(cancelled.ok());
    CHECK(cancelled.error().kind == core::ErrorKind::Cancelled);
}

#ifdef _WIN32
TEST_CASE("bounded HTTPS rejects an untrusted loopback TLS certificate") {
    using namespace std::chrono_literals;
    UntrustedTlsPeer peer;
    REQUIRE(peer.PrepareCertificate());
    REQUIRE(peer.AcquireCredential() == SEC_E_OK);
    const auto port = peer.Start();
    REQUIRE(port > 0);

    std::atomic<bool> cancel{false};
    const auto result = platform::HttpGetBoundedHttpsText(
        "https://tls-fixture.invalid/", "http://127.0.0.1:" + std::to_string(port),
        {1024, 5s}, cancel);
    REQUIRE_FALSE(result);
    INFO(result.error().message);
    CHECK(result.error().kind == core::ErrorKind::DownloadFailed);
    CHECK(peer.SentServerFlight());
    CHECK(result.error().message == "HTTPS server certificate validation failed");
}

TEST_CASE("bounded HTTPS configures WinHTTP to disallow downgrade redirects") {
    WinHttpHandle session{::WinHttpOpen(L"ClashYuka test", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    REQUIRE(session.value != nullptr);
    WinHttpHandle connection{::WinHttpConnect(session.value, L"127.0.0.1", 443, 0)};
    REQUIRE(connection.value != nullptr);
    WinHttpHandle request{::WinHttpOpenRequest(connection.value, L"GET", L"/", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    REQUIRE(request.value != nullptr);

    // Start from a non-default policy so dropping the production setter cannot pass.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    REQUIRE(::WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                              &policy, sizeof(policy)) != FALSE);
    REQUIRE(platform::detail::ConfigureBoundedHttpsRequest(request.value));
    DWORD size = sizeof(policy);
    REQUIRE(::WinHttpQueryOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                                &policy, &size) != FALSE);
    CHECK(policy == WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP);

    REQUIRE(platform::detail::ConfigureBoundedHttpsRequest(request.value, false));
    size = sizeof(policy);
    REQUIRE(::WinHttpQueryOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                                &policy, &size) != FALSE);
    CHECK(policy == WINHTTP_OPTION_REDIRECT_POLICY_NEVER);
}

TEST_CASE("bounded HTTPS rejects plaintext and redacts URL details") {
    REQUIRE(platform::HttpsTextAvailable());
    std::atomic<bool> cancel{false};
    const auto invalid = platform::HttpGetBoundedHttpsText(
        "http://127.0.0.1:1/?token=private", {}, {}, cancel);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == core::ErrorKind::InvalidConfig);
    CHECK(invalid.error().message.find("private") == std::string::npos);
    cancel = true;
    const auto aborted = platform::HttpGetBoundedHttpsText(
        "https://127.0.0.1:1/?token=private", {}, {}, cancel);
    REQUIRE_FALSE(aborted);
    CHECK(aborted.error().kind == core::ErrorKind::Cancelled);
}

TEST_CASE("bounded HTTPS interrupts a stalled proxy handshake") {
    using namespace std::chrono_literals;
    std::atomic<bool> entered{false};
    std::atomic<bool> cancel{false};
    MockServer proxy;
    proxy.server().set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
        if (req.method == "CONNECT") {
            entered = true;
            std::this_thread::sleep_for(2s);
        }
        res.status = 502;
        return httplib::Server::HandlerResponse::Handled;
    });
    REQUIRE(proxy.Start());
    const auto proxyUrl = "http://127.0.0.1:" + std::to_string(proxy.port());
    constexpr auto url = "https://subscription.invalid/?token=private";

    SUBCASE("deadline") {
        const auto result = platform::HttpGetBoundedHttpsText(url, proxyUrl, {1024, 750ms}, cancel);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == core::ErrorKind::Timeout);
    }
    SUBCASE("in-flight cancel") {
        std::jthread stop([&] {
            const auto deadline = std::chrono::steady_clock::now() + 1500ms;
            while (!entered.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(5ms);
            }
            cancel = true;
        });
        const auto result = platform::HttpGetBoundedHttpsText(url, proxyUrl, {1024, 3s}, cancel);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == core::ErrorKind::Cancelled);
        CHECK(entered.load());
    }
}
#endif
