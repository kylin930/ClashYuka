#pragma once

#include <windows.h>
#include <winhttp.h>

namespace clashyuka::platform::detail {

bool ConfigureBoundedHttpsRequest(HINTERNET request, bool followRedirects = true);

}  // namespace clashyuka::platform::detail
