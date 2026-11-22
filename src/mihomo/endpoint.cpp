#include "clashyuka/mihomo/endpoint.h"

#include <cctype>

namespace clashyuka::mihomo {
namespace {

bool IsUnreserved(unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
}

bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string EncodePathSegment(std::string_view segment) {
    static constexpr char kHex[] = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(segment.size() * 3);
    for (const unsigned char c : segment) {
        if (IsUnreserved(c)) {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[c >> 4]);
            encoded.push_back(kHex[c & 0x0F]);
        }
    }
    return encoded;
}

std::string EncodeQueryComponent(std::string_view value) {
    return EncodePathSegment(value);
}

bool IsWildcardAddress(std::string_view host) {
    return host.empty() || host == "*" || host == "0.0.0.0" || host == "::" ||
           host == "[::]";
}

bool IsLoopbackHost(std::string_view host) {
    return host == "127.0.0.1" || host == "::1" || host == "[::1]" ||
           EqualsIgnoreCase(host, "localhost");
}

std::string BuildWebSocketUrl(std::string_view host, int port, std::string_view path) {
    std::string url = "ws://";
    if (host.find(':') != std::string_view::npos && !host.starts_with('[')) {
        url.push_back('[');
        url.append(host);
        url.push_back(']');
    } else {
        url.append(host);
    }
    url.push_back(':');
    url.append(std::to_string(port));
    if (!path.empty() && path.front() != '/') {
        url.push_back('/');
    }
    url.append(path);
    return url;
}

}  // namespace clashyuka::mihomo
