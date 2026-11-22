#pragma once

#include <string_view>

namespace clashyuka::core {

// Records a startup milestone relative to process start. The line is written to
// stderr so a console/debugger run can verify real timings without a UI.
void MarkStartup(std::string_view stage);

// Milliseconds elapsed since the first call (or process start for the first mark).
double MillisecondsSinceStartup();

}  // namespace clashyuka::core
