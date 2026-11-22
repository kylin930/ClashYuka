#pragma once

#include <memory>
#include <string>

namespace clashyuka::network {

// Minimal holder for a Mihomo External Controller endpoint.
// P0 scope: endpoint construction only, no HTTP or WebSocket requests are issued.
// Requests must never run on the GUI thread once they are implemented.
class MihomoClient {
public:
    MihomoClient(std::string host, int port);
    ~MihomoClient();

    MihomoClient(const MihomoClient&) = delete;
    MihomoClient& operator=(const MihomoClient&) = delete;
    MihomoClient(MihomoClient&&) noexcept;
    MihomoClient& operator=(MihomoClient&&) noexcept;

    std::string Endpoint() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clashyuka::network
