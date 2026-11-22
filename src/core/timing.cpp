#include "clashyuka/core/timing.h"

#include <chrono>
#include <cstdio>

namespace clashyuka::core {
namespace {

std::chrono::steady_clock::time_point StartPoint() {
    static const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    return start;
}

}  // namespace

void MarkStartup(std::string_view stage) {
    const double elapsed = MillisecondsSinceStartup();
    std::fprintf(stderr, "[clashyuka-timing] %.*s: %.1f ms\n", static_cast<int>(stage.size()),
                 stage.data(), elapsed);
    std::fflush(stderr);
}

double MillisecondsSinceStartup() {
    const auto elapsed = std::chrono::steady_clock::now() - StartPoint();
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

}  // namespace clashyuka::core
