#pragma once

#include <windows.h>
#include <winhttp.h>

namespace clashyuka::platform::detail {

bool ConfigureBoundedHttpsRequest(HINTERNET request);

}  // namespace clashyuka::platform::detail
