# ClashYuka 顶层 CMake 项目文件
cmake_minimum_required(VERSION 3.20)

project("ClashYuka" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================
# MSVC 设置：UTF-8 + 静态运行时 (/MT)
# 所有代码和依赖均使用 /MT 静态运行时，生成的 exe 不依赖 VC runtime DLL。
# wxWidgets 使用 wxBUILD_USE_STATIC_RUNTIME=ON 重新编译，库在 build_mt/lib/vc_x64_lib。
# =========================================================
if(MSVC)
    add_compile_options("/utf-8")
    add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS _ITERATOR_DEBUG_LEVEL=0 UNICODE _UNICODE)
    # 静态运行时 (/MT)，所有子项目继承
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
endif()

# =========================================================
# wxWidgets 静态库配置 (手动指定路径，/MT 静态运行时)
# 库文件在 build_mt/lib/vc_x64_lib（/MT 静态运行时重新编译）
# =========================================================
set(WX_ROOT "G:/Documents/wxwidgetsBuild")
set(WX_LIB_DIR "${WX_ROOT}/build_mt/lib/vc_x64_lib")
set(WX_CFG "mswu")

# 手动设置 wxWidgets 变量（绕过 FindwxWidgets.cmake 的路径限制）
set(wxWidgets_FOUND TRUE)
set(wxWidgets_INCLUDE_DIRS
    "${WX_ROOT}/include"
    "${WX_LIB_DIR}/${WX_CFG}"
)
set(wxWidgets_LIBRARY_DIRS "${WX_LIB_DIR}")
set(wxWidgets_DEFINITIONS "__WXMSW__;wxUSE_GUI;wxBUILD_LEVEL_FILE=1")
set(wxWidgets_LIBRARIES
    ${WX_LIB_DIR}/wxmsw33u_core.lib
    ${WX_LIB_DIR}/wxmsw33u_adv.lib
    ${WX_LIB_DIR}/wxmsw33u_aui.lib
    ${WX_LIB_DIR}/wxmsw33u_html.lib
    ${WX_LIB_DIR}/wxmsw33u_stc.lib
    ${WX_LIB_DIR}/wxmsw33u_xrc.lib
    ${WX_LIB_DIR}/wxmsw33u_propgrid.lib
    ${WX_LIB_DIR}/wxmsw33u_ribbon.lib
    ${WX_LIB_DIR}/wxmsw33u_richtext.lib
    ${WX_LIB_DIR}/wxbase33u.lib
    ${WX_LIB_DIR}/wxbase33u_net.lib
    ${WX_LIB_DIR}/wxbase33u_xml.lib
    ${WX_LIB_DIR}/wxscintilla.lib
    ${WX_LIB_DIR}/wxlexilla.lib
    ${WX_LIB_DIR}/wxpng.lib
    ${WX_LIB_DIR}/wxzlib.lib
    ${WX_LIB_DIR}/wxjpeg.lib
    ${WX_LIB_DIR}/wxtiff.lib
    ${WX_LIB_DIR}/wxexpat.lib
    ${WX_LIB_DIR}/wxregexu.lib
    # Windows 系统库
    kernel32.lib user32.lib gdi32.lib comdlg32.lib comctl32.lib
    ole32.lib oleaut32.lib uuid.lib advapi32.lib shell32.lib
    wininet.lib wsock32.lib ws2_32.lib winmm.lib
    imm32.lib shlwapi.lib version.lib uxtheme.lib
    dwmapi.lib netapi32.lib rpcrt4.lib
)
include(${CMAKE_ROOT}/Modules/UsewxWidgets.cmake)

# 应用目标假设 wxWidgets 以 CONFIG 模式提供 wx::core / wx::base 目标。
# 此处的 wxWidgets 为 makefile 静态构建（MODULE 模式），故创建别名目标。
if(NOT TARGET wx::base)
    add_library(wx::base INTERFACE IMPORTED)
    target_include_directories(wx::base INTERFACE ${wxWidgets_INCLUDE_DIRS})
    target_link_libraries(wx::base INTERFACE ${wxWidgets_LIBRARIES})
endif()
if(NOT TARGET wx::core)
    add_library(wx::core INTERFACE IMPORTED)
    target_link_libraries(wx::core INTERFACE wx::base)
endif()
if(NOT TARGET wx::aui)
    add_library(wx::aui INTERFACE IMPORTED)
    target_link_libraries(wx::aui INTERFACE wx::base)
endif()

# =========================================================
# 第三方依赖 (FetchContent)
# =========================================================
include(FetchContent)

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# yaml-cpp
FetchContent_Declare(
    yaml_cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
    GIT_SHALLOW TRUE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml_cpp)

# mbedtls（TLS 后端，供 IXWebSocket HTTPS 使用）
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.2
    GIT_SHALLOW TRUE
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mbedtls)

# 预设 mbedtls 变量，绕过 IXWebSocket 的 find_package(MbedTLS)。
# 原因：mbedtls 由 FetchContent 构建，库文件在 build 阶段才生成；
# 而 find_package 在 configure 阶段运行时 find_library 找不到 .lib 文件。
# 这里直接将变量指向 CMake 目标名（mbedtls/mbedx509/mbedcrypto），
# IXWebSocket 的 target_link_libraries(${MBEDTLS_LIBRARIES}) 会解析为目标依赖。
set(MBEDTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(MBEDTLS_LIBRARIES "mbedtls;mbedx509;mbedcrypto" CACHE STRING "" FORCE)
# mbedtls 3.x 有 build_info.h，FindMbedTLS.cmake 会据此设此变量；
# 由于我们跳过了 find_package，需手动设置以启用 IXWebSocket 的 mbedtls 3.x 适配分支
set(MBEDTLS_VERSION_GREATER_THAN_3 TRUE CACHE BOOL "" FORCE)

# IXWebSocket (HTTP + WebSocket)。启用 TLS（mbedtls）以支持 HTTPS 请求：
# GitHub API / 内核下载 / 远程订阅均需 HTTPS。
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
    GIT_SHALLOW TRUE
)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(IXWEBSOCKET_USE_TLS ON CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# mbedtls 3.x 适配：IXWebSocket 的 if(NOT MBEDTLS_FOUND) 分支被我们跳过，
# 需手动添加编译定义以启用 5 参数版本的 mbedtls_pk_parse_keyfile 调用
target_compile_definitions(ixwebsocket PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)

# =========================================================
# 子项目
# =========================================================
add_subdirectory("ClashYuka")
