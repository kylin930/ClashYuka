#include "clashyuka/network/mihomo_client.h"

#include <utility>

#include <httplib.h>

namespace clashyuka::network {

struct MihomoClient::Impl {
    Impl(std::string host, int port)
        : client(std::move(host), port),
          endpoint("http://" + client.host() + ":" + std::to_string(port)) {}

    httplib::Client client;
    std::string endpoint;
};

MihomoClient::MihomoClient(std::string host, int port)
    : impl_(std::make_unique<Impl>(std::move(host), port)) {}

MihomoClient::~MihomoClient() = default;

MihomoClient::MihomoClient(MihomoClient&&) noexcept = default;
MihomoClient& MihomoClient::operator=(MihomoClient&&) noexcept = default;

std::string MihomoClient::Endpoint() const {
    return impl_->endpoint;
}

}  // namespace clashyuka::network
