#pragma once

#include <string>

namespace clashyuka::core {

struct RuntimeInfo {
    std::string platformName;
    bool elevated = false;
    std::string compiler;
    std::string cppStandard;
};

RuntimeInfo GetRuntimeInfo();

}  // namespace clashyuka::core
