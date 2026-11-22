#pragma once

#include <string>
#include <string_view>

namespace clashyuka::mihomo {

// Percent-encodes a single URL path segment (RFC 3986 unreserved characters are
// kept, everything else including '/' is escaped).
std::string EncodePathSegment(std::string_view segment);

// Hosts that cannot be used as a request target.
bool IsWildcardAddress(std::string_view host);

bool IsLoopbackHost(std::string_view host);

// Builds ws://host:port/path, bracketing IPv6 literals when needed.
std::string BuildWebSocketUrl(std::string_view host, int port, std::string_view path);

}  // namespace clashyuka::mihomo
