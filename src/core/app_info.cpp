#include "clashyuka/core/app_info.h"

#include "clashyuka/platform/platform_services.h"

namespace clashyuka::core {
namespace {

std::string CompilerName() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "Unknown";
#endif
}

std::string CppStandardName() {
#if defined(__cplusplus) && __cplusplus >= 202302L
    return "C++23 (" + std::to_string(__cplusplus) + "L)";
#elif defined(__cplusplus) && __cplusplus >= 202002L
    return "C++20 (" + std::to_string(__cplusplus) + "L)";
#elif defined(__cplusplus) && __cplusplus >= 201703L
    return "C++17 (" + std::to_string(__cplusplus) + "L)";
#else
    return "Pre-C++17 (" + std::to_string(__cplusplus) + "L)";
#endif
}

}  // namespace

RuntimeInfo GetRuntimeInfo() {
    const auto services = platform::CreatePlatformServices();

    RuntimeInfo info;
    info.platformName = services->Name();
    info.elevated = services->IsElevated();
    info.compiler = CompilerName();
    info.cppStandard = CppStandardName();
    return info;
}

}  // namespace clashyuka::core
