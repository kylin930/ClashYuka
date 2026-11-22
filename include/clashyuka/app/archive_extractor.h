#pragma once

#include <memory>

#include "clashyuka/runtime/kernel_installer.h"

namespace clashyuka::app {

// wxWidgets-backed ZIP/GZIP extractor. Only the Mihomo executable is written,
// and unsafe member paths are rejected.
std::unique_ptr<runtime::KernelArchiveExtractor> CreateWxArchiveExtractor();

}  // namespace clashyuka::app
