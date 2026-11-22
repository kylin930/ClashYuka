cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project(ClashYuka VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CLASHYUKA_BUILD_TESTS "Build ClashYuka unit tests" ON)

# The wxWidgets static libraries used on Windows were built against the release
# dynamic CRT (/MD). Every configuration links the same CRT to stay binary
# compatible with them.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

add_library(clashyuka_options INTERFACE)
if(MSVC)
  target_compile_options(clashyuka_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  # wxWidgets static libraries are release builds (NDEBUG), so their STL
  # iterator debug level is 0. Debug builds must match it or the linker reports
  # an _ITERATOR_DEBUG_LEVEL mismatch.
  target_compile_definitions(clashyuka_options INTERFACE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
else()
  target_compile_options(clashyuka_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(cmake/Dependencies.cmake)

if(WIN32)
  set(CLASHYUKA_PLATFORM_SOURCE src/platform/windows/platform_services_windows.cpp)
elseif(APPLE)
  set(CLASHYUKA_PLATFORM_SOURCE src/platform/macos/platform_services_macos.cpp)
elseif(UNIX)
  set(CLASHYUKA_PLATFORM_SOURCE src/platform/linux/platform_services_linux.cpp)
else()
  message(FATAL_ERROR "ClashYuka does not support this platform yet.")
endif()

add_library(clashyuka_platform STATIC ${CLASHYUKA_PLATFORM_SOURCE})
target_include_directories(clashyuka_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_platform PUBLIC clashyuka_options)
if(WIN32)
  target_link_libraries(clashyuka_platform PUBLIC advapi32)
endif()

add_library(clashyuka_core STATIC
  src/core/app_info.cpp
  src/network/mihomo_client.cpp
)
target_include_directories(clashyuka_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_core
  PUBLIC
    clashyuka_platform
    httplib::httplib
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
)

# --- wxWidgets -------------------------------------------------------------
if(WIN32)
  if(NOT WXWIDGETS_ROOT)
    message(FATAL_ERROR
      "WXWIDGETS_ROOT is not set. Configure through CMakeUserPresets.json "
      "(see README.md) or pass -DWXWIDGETS_ROOT=<path>.")
  endif()
  set(wxWidgets_ROOT_DIR "${WXWIDGETS_ROOT}")
  if(WXWIDGETS_LIB_DIR)
    set(wxWidgets_LIB_DIR "${WXWIDGETS_LIB_DIR}")
  endif()
  set(wxWidgets_CONFIGURATION mswu)
endif()

find_package(wxWidgets REQUIRED COMPONENTS core base)

add_executable(ClashYuka WIN32
  src/main.cpp
  src/app/app.cpp
  src/ui/main_frame.cpp
)
target_include_directories(ClashYuka PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(ClashYuka PRIVATE ${wxWidgets_DEFINITIONS})
target_link_libraries(ClashYuka PRIVATE clashyuka_core ${wxWidgets_LIBRARIES})
set_target_properties(ClashYuka PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- tests -----------------------------------------------------------------
if(CLASHYUKA_BUILD_TESTS)
  enable_testing()

  add_executable(clashyuka_tests tests/test_main.cpp)
  target_include_directories(clashyuka_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(clashyuka_tests PRIVATE clashyuka_core doctest::doctest)
  set_target_properties(clashyuka_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME clashyuka_tests COMMAND clashyuka_tests)
endif()
