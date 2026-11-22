#pragma once

#include <string>
#include <string_view>

namespace clashyuka::runtime {

std::string Sha256Hex(std::string_view data);

// Returns a lowercase hex digest, or an empty string when the file cannot be
// read. The file is streamed in fixed-size chunks.
std::string Sha256File(const std::string& path, std::string* error = nullptr);

}  // namespace clashyuka::runtime
