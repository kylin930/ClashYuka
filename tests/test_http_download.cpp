#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include "clashyuka/platform/http_download.h"
#include "mock/mock_server.h"

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
