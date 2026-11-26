# ClashYuka 可执行目标 CMakeLists

# 源文件（随阶段推进追加）
set(CLASHYUKA_SOURCES
    src/main.cpp
    src/core/utils/dirs.cpp
    src/core/config/verge_config.cpp
    src/core/config/clash_config.cpp
    src/core/config/profile.cpp
    src/core/clash/process.cpp
    src/core/clash/types.cpp
    src/core/clash/api_client.cpp
    src/core/clash/ws_client.cpp
    src/core/clash/downloader.cpp
    src/core/clash/core_manager.cpp
    src/core/enhance/merge.cpp
    src/core/enhance/builder.cpp
    src/ui/main_frame.cpp
    src/ui/theme.cpp
    src/core/app_context.cpp
    src/core/tray.cpp
    src/core/hotkey.cpp
    src/core/sysopt.cpp
    src/core/autostart.cpp
    src/core/i18n.cpp
    src/core/updater.cpp
    src/core/migration.cpp
    src/core/singleton.cpp
    src/core/resources.cpp
    src/core/service_manager.cpp
    src/core/enhance/js_engine.cpp
    src/core/enhance/lua_engine.cpp
    src/ui/pages/dashboard/page.cpp
    src/ui/pages/proxies/page.cpp
    src/ui/pages/profiles/page.cpp
    src/ui/pages/connections/page.cpp
    src/ui/pages/rules/page.cpp
    src/ui/pages/logs/page.cpp
    src/ui/pages/providers/page.cpp
    src/ui/pages/settings/page.cpp
)

add_executable(ClashYuka WIN32 ${CLASHYUKA_SOURCES})

target_include_directories(ClashYuka PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(ClashYuka PRIVATE
    wx::core
    wx::base
    wx::aui
    nlohmann_json::nlohmann_json
    yaml-cpp
    ixwebsocket
)

# Windows 系统库（WinHTTP 用于内核下载）
if(WIN32)
    target_link_libraries(ClashYuka PRIVATE winhttp)
endif()

if(MSVC)
    target_compile_options(ClashYuka PRIVATE "/utf-8")
endif()

# =========================================================
# 测试（默认关闭）：mihomo API 服务层 Mock 测试 + 真实内核集成测试
# 开启：cmake -DCLASHYUKA_BUILD_TESTS=ON ...
# =========================================================
option(CLASHYUKA_BUILD_TESTS "Build ClashYuka API tests" OFF)

if(CLASHYUKA_BUILD_TESTS)
    add_executable(clashyuka_api_tests
        tests/api_tests.cpp
        src/core/app_context.cpp
        src/core/clash/api_client.cpp
        src/core/clash/types.cpp
        src/core/clash/ws_client.cpp
        src/core/clash/process.cpp
        src/core/clash/core_manager.cpp
        src/core/config/verge_config.cpp
        src/core/config/clash_config.cpp
        src/core/config/profile.cpp
        src/core/enhance/builder.cpp
        src/core/enhance/merge.cpp
        src/core/enhance/js_engine.cpp
        src/core/enhance/lua_engine.cpp
        src/core/migration.cpp
        src/core/resources.cpp
        src/core/service_manager.cpp
        src/core/utils/dirs.cpp
    )

    target_include_directories(clashyuka_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(clashyuka_api_tests PRIVATE
        nlohmann_json::nlohmann_json
        yaml-cpp
        ixwebsocket
    )

    if(WIN32)
        target_link_libraries(clashyuka_api_tests PRIVATE winhttp advapi32 shell32)
    endif()

    if(MSVC)
        target_compile_options(clashyuka_api_tests PRIVATE "/utf-8")
    endif()
endif()

# =========================================================
# 服务模式可执行文件（仅 Windows）
# =========================================================
if(WIN32)
    set(CLASHYUKA_SERVICE_SOURCES
        src/service/main.cpp
    )

    add_executable(clashyuka-service ${CLASHYUKA_SERVICE_SOURCES})

    target_include_directories(clashyuka-service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # 服务程序需要链接 advapi32（SCM API）
    target_link_libraries(clashyuka-service PRIVATE advapi32)

    if(MSVC)
        target_compile_options(clashyuka-service PRIVATE "/utf-8")
    endif()

    # 输出名：clashyuka-service.exe
    set_target_properties(clashyuka-service PROPERTIES
        OUTPUT_NAME "clashyuka-service"
    )

    # 构建后将 service exe 复制到 ClashYuka.exe 同级的 resources/ 目录。
    # 原因：ServiceManager::service_exe() 返回 app_resources_dir()/"clashyuka-service.exe"，
    # 而 app_resources_dir() = <exe_dir>/resources。若不复制，运行时找不到 service exe，
    # 服务模式安装/启动会直接失败。
    add_custom_command(TARGET clashyuka-service POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:ClashYuka>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:clashyuka-service>
            $<TARGET_FILE_DIR:ClashYuka>/resources/clashyuka-service.exe
        COMMENT "Copying clashyuka-service.exe to resources/"
    )
endif()
