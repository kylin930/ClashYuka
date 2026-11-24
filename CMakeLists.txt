cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project(ClashYuka VERSION 0.2.0 LANGUAGES C CXX)

# CLI and Visual Studio intentionally use different binary directories:
#   out/build/windows-x64     (CLI presets)
#   out/build/vs/windows-x64  (vs-windows-x64 preset used by Visual Studio)
# Never point both at the same directory; Ninja logs would be rewritten
# concurrently.

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
find_package(Threads REQUIRED)

if(WIN32)
  set(CLASHYUKA_PLATFORM_SOURCES
    src/platform/windows/platform_services_windows.cpp
    src/platform/windows/http_download_windows.cpp
    src/platform/windows/system_proxy_windows.cpp
  )
elseif(APPLE)
  set(CLASHYUKA_PLATFORM_SOURCES
    src/platform/macos/platform_services_macos.cpp
    src/platform/posix/http_download_posix.cpp
    src/platform/posix/system_proxy_posix.cpp
  )
elseif(UNIX)
  set(CLASHYUKA_PLATFORM_SOURCES
    src/platform/linux/platform_services_linux.cpp
    src/platform/posix/http_download_posix.cpp
    src/platform/posix/system_proxy_posix.cpp
  )
else()
  message(FATAL_ERROR "ClashYuka does not support this platform yet.")
endif()

add_library(clashyuka_platform STATIC ${CLASHYUKA_PLATFORM_SOURCES})
target_include_directories(clashyuka_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_platform PUBLIC clashyuka_options nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(clashyuka_platform PUBLIC advapi32 winhttp wininet)
endif()

add_library(clashyuka_core STATIC
  src/core/app_info.cpp
  src/core/timing.cpp
)
target_include_directories(clashyuka_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_core
  PUBLIC
    clashyuka_platform
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
)

# --- Mihomo communication layer (no wxWidgets dependency) ------------------
add_library(clashyuka_mihomo STATIC
  src/mihomo/types.cpp
  src/mihomo/json.cpp
  src/mihomo/endpoint.cpp
  src/mihomo/api_client.cpp
  src/mihomo/event_client.cpp
)
target_include_directories(clashyuka_mihomo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_mihomo
  PUBLIC
    clashyuka_options
    httplib::httplib
    nlohmann_json::nlohmann_json
    Threads::Threads
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

# --- Mihomo process supervision (platform backend) -------------------------
if(WIN32)
  set(CLASHYUKA_PROCESS_BACKEND src/platform/windows/process_backend_windows.cpp)
else()
  set(CLASHYUKA_PROCESS_BACKEND src/platform/posix/process_backend_posix.cpp)
endif()

add_library(clashyuka_process STATIC
  src/mihomo/process.cpp
  ${CLASHYUKA_PROCESS_BACKEND}
)
target_include_directories(clashyuka_process PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_process PUBLIC clashyuka_options Threads::Threads)

# --- Runtime orchestration (settings, runtime config, kernel session) --------
# Set only for distribution builds with a verified official GitHub repository.
set(CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY "" CACHE STRING
    "Official ClashYuka GitHub release repository (owner/repo, empty disables checks)")
# Stable vX.Y.Z or X.Y.Z release tags. Windows assets use
# ClashYuka[-<version>]-windows-<x64|arm64|x86>.<zip|msi|exe>.
if(NOT CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY STREQUAL "" AND
   NOT CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY MATCHES "^[A-Za-z0-9-]+/[A-Za-z0-9_.-]+$")
  message(FATAL_ERROR "CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY must be GitHub owner/repo")
endif()

add_library(clashyuka_runtime STATIC
  src/runtime/app_update.cpp
  src/runtime/settings_store.cpp
  src/runtime/profile_store.cpp
  src/runtime/subscription_service.cpp
  src/runtime/runtime_config.cpp
  src/runtime/kernel_session.cpp
  src/runtime/proxy_service.cpp
  src/runtime/kernel_release.cpp
  src/runtime/sha256.cpp
  src/runtime/kernel_installer.cpp
)
target_compile_definitions(clashyuka_runtime PRIVATE
  CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY="${CLASHYUKA_OFFICIAL_RELEASE_REPOSITORY}"
  CLASHYUKA_BUILD_VERSION="${PROJECT_VERSION}"
)
target_include_directories(clashyuka_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clashyuka_runtime
  PUBLIC
    clashyuka_options
    clashyuka_core
    clashyuka_mihomo
    clashyuka_process
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Threads::Threads
)

# --- MD3 design system ------------------------------------------------------
add_library(clashyuka_ui STATIC
  src/ui/theme/color_scheme.cpp
  src/ui/theme/theme_manager.cpp
  src/ui/theme/dpi.cpp
  src/ui/drawing/painter.cpp
  src/ui/drawing/icons.cpp
  src/ui/drawing/animation.cpp
  src/ui/drawing/ripple.cpp
  src/ui/components/control_base.cpp
  src/ui/components/divider.cpp
  src/ui/components/surface_card.cpp
  src/ui/components/button.cpp
  src/ui/components/icon_button.cpp
  src/ui/components/switch.cpp
  src/ui/components/list_item.cpp
  src/ui/components/navigation_rail.cpp
  src/ui/components/tab_bar.cpp
  src/ui/components/dialog.cpp
  src/ui/components/snackbar.cpp
  src/ui/components/progress_indicator.cpp
  src/ui/components/text_block.cpp
  src/ui/components/search_field.cpp
)
target_include_directories(clashyuka_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(clashyuka_ui PUBLIC ${wxWidgets_DEFINITIONS})
if(MSVC)
  target_compile_definitions(clashyuka_ui PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(clashyuka_ui PUBLIC clashyuka_options ${wxWidgets_LIBRARIES})

# --- Application shell (state, controller, pages, main window) --------------
add_library(clashyuka_app STATIC
  src/app/app_state.cpp
  src/app/app_controller.cpp
  src/app/archive_extractor.cpp
  src/app/page_registry.cpp
  src/ui/main_window.cpp
  src/ui/pages/page_base.cpp
  src/ui/pages/overview_page.cpp
  src/ui/pages/proxies_page.cpp
  src/ui/pages/connections_page.cpp
  src/ui/pages/rules_page.cpp
  src/ui/pages/profiles_page.cpp
  src/ui/pages/logs_page.cpp
  src/ui/pages/settings_page.cpp
)
target_include_directories(clashyuka_app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(clashyuka_app PUBLIC ${wxWidgets_DEFINITIONS})
if(MSVC)
  target_compile_definitions(clashyuka_app PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(clashyuka_app PUBLIC
  clashyuka_core
  clashyuka_runtime
  clashyuka_ui
  ${wxWidgets_LIBRARIES}
)

add_executable(ClashYuka WIN32
  src/main.cpp
  src/app/app.cpp
)
target_include_directories(ClashYuka PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(ClashYuka PRIVATE ${wxWidgets_DEFINITIONS})
if(MSVC)
  target_compile_definitions(ClashYuka PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(ClashYuka PRIVATE
  clashyuka_app
  clashyuka_core
  clashyuka_mihomo
  clashyuka_process
  clashyuka_ui
  ${wxWidgets_LIBRARIES}
)
set_target_properties(ClashYuka PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- tests -----------------------------------------------------------------
if(CLASHYUKA_BUILD_TESTS)
  enable_testing()

  add_executable(clashyuka_tests
    tests/test_main.cpp
    tests/test_models.cpp
    tests/test_api_client.cpp
    tests/test_event_client.cpp
    tests/test_http_download.cpp
  )
  if(WIN32)
    target_sources(clashyuka_tests PRIVATE tests/test_system_proxy.cpp)
    target_link_libraries(clashyuka_tests PRIVATE crypt32 secur32)
  endif()
  target_include_directories(clashyuka_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(clashyuka_tests PRIVATE
    clashyuka_core
    clashyuka_mihomo
    doctest::doctest
  )
  set_target_properties(clashyuka_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_tests COMMAND clashyuka_tests)

  add_executable(clashyuka_ui_tests tests/test_ui.cpp)
  target_link_libraries(clashyuka_ui_tests PRIVATE clashyuka_ui doctest::doctest)
  set_target_properties(clashyuka_ui_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_ui_tests COMMAND clashyuka_ui_tests)

  add_executable(clashyuka_app_tests tests/test_app.cpp)
  target_link_libraries(clashyuka_app_tests PRIVATE clashyuka_app doctest::doctest)
  set_target_properties(clashyuka_app_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_app_tests COMMAND clashyuka_app_tests)

  add_executable(clashyuka_runtime_tests tests/test_runtime.cpp tests/test_app_update.cpp)
  target_link_libraries(clashyuka_runtime_tests PRIVATE
    clashyuka_runtime
    doctest::doctest
  )
  set_target_properties(clashyuka_runtime_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_runtime_tests COMMAND clashyuka_runtime_tests)

  add_executable(clashyuka_profile_tests tests/test_profile_store.cpp)
  target_link_libraries(clashyuka_profile_tests PRIVATE clashyuka_runtime doctest::doctest)
  set_target_properties(clashyuka_profile_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_profile_tests COMMAND clashyuka_profile_tests)

  add_executable(clashyuka_subscription_tests tests/test_subscription_service.cpp)
  target_link_libraries(clashyuka_subscription_tests PRIVATE clashyuka_runtime doctest::doctest)
  set_target_properties(clashyuka_subscription_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_subscription_tests COMMAND clashyuka_subscription_tests)

  add_executable(clashyuka_proxy_tests tests/test_proxy_service.cpp)
  target_link_libraries(clashyuka_proxy_tests PRIVATE
    clashyuka_runtime
    doctest::doctest
  )
  set_target_properties(clashyuka_proxy_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_proxy_tests COMMAND clashyuka_proxy_tests)

  add_executable(clashyuka_install_tests tests/test_installer.cpp)
  target_link_libraries(clashyuka_install_tests PRIVATE
    clashyuka_app
    doctest::doctest
  )
  set_target_properties(clashyuka_install_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_install_tests COMMAND clashyuka_install_tests)

  add_executable(clashyuka_process_probe tests/helpers/process_probe.cpp)
  set_target_properties(clashyuka_process_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_executable(clashyuka_process_tests tests/process/test_process.cpp)
  target_link_libraries(clashyuka_process_tests PRIVATE
    clashyuka_process
    doctest::doctest
  )
  target_compile_definitions(clashyuka_process_tests PRIVATE
    CLASHYUKA_PROCESS_PROBE_PATH="$<TARGET_FILE:clashyuka_process_probe>"
  )
  add_dependencies(clashyuka_process_tests clashyuka_process_probe)
  set_target_properties(clashyuka_process_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME clashyuka_process_tests COMMAND clashyuka_process_tests)

  # Real kernel verification; requires an externally supplied mihomo binary, so
  # it is built but not registered with CTest.
  add_executable(clashyuka_mihomo_integration tests/integration/mihomo_integration.cpp)
  target_link_libraries(clashyuka_mihomo_integration PRIVATE
    clashyuka_mihomo
    clashyuka_process
  )
  set_target_properties(clashyuka_mihomo_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endif()
