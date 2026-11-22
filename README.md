# ClashYuka

基于 Mihomo 的轻量级原生桌面代理客户端。P0 阶段只包含跨平台工程基础与最小 GUI，不含代理功能。

## 技术栈

- C++20
- CMake（唯一构建系统，Ninja Multi-Config 生成器）
- wxWidgets 3.3.1（Windows 上使用本机已编译的静态库）
- cpp-httplib / nlohmann-json / yaml-cpp（通过 FetchContent 固定版本）
- doctest（仅测试）

## 平台状态

| 平台 | 状态 |
| --- | --- |
| Windows 10/11 x64 (MSVC) | 已实际配置、构建 Debug/Release、运行 GUI、执行测试 |
| macOS (AppleClang) | 仅提供 CMake Preset，未在 macOS 上构建验证 |
| Linux (GCC) | 仅提供 CMake Preset，未在 Linux 上构建验证 |

## 目录结构

```
ClashYuka/
├── CMakeLists.txt
├── CMakePresets.json          # 提交到 Git 的通用预设
├── CMakeUserPresets.json      # 本机路径与代理，已被 .gitignore 忽略
├── cmake/Dependencies.cmake   # 固定版本第三方依赖
├── include/clashyuka/         # 公共接口（platform / core / network）
├── src/
│   ├── main.cpp
│   ├── app/                   # wxApp
│   ├── core/                  # 平台无关业务代码
│   ├── network/               # Mihomo 通信接口骨架
│   ├── platform/{windows,macos,linux}/
│   └── ui/                    # wxFrame
├── tests/                     # doctest 单元测试
└── out/build/windows-x64/     # 构建输出（不提交）
```

## 开发环境要求

Windows 已验证的环境：

- Visual Studio 2022 Community 17.13.6（工作负载包含「使用 C++ 的桌面开发」与 CMake 工具）
- MSVC 19.43.34808（v143 工具集）
- Windows SDK 10.0.26100.0
- CMake 3.31.5
- Ninja 1.12.1
- Git 2.47

macOS 需要 Xcode Command Line Tools 与 CMake；Linux 需要 g++（C++20）、CMake、Ninja，以及系统安装的 wxWidgets 开发包。

## wxWidgets 静态库（Windows）

使用本机已有构建，不重新下载或编译：

- 根目录：`G:/Documents/wxwidgetsBuild`
- 静态库目录：`G:/Documents/wxwidgetsBuild/lib/vc_x64_lib`
- 版本 3.3.1，Unicode，x64，静态库
- 仅存在 Release 库（`wxbase33u.lib`、`wxmsw33u_core.lib` 等，无 `*d.lib`）
- CRT：`/MD`（release 动态 CRT，`MSVCRT`/`msvcprt`）

为与现成库保持二进制兼容，工程统一使用 `/MD`，且 Debug 配置额外定义 `_ITERATOR_DEBUG_LEVEL=0`。不要将 Debug 配置改成 `/MDd`，否则会与现有 wxWidgets 库产生链接冲突。

个人绝对路径只写在 `CMakeUserPresets.json`：

```json
{
  "configurePresets": [
    {
      "name": "windows-x64-release",
      "inherits": "windows-x64-local-base",
      "displayName": "Windows x64 Release (local wxWidgets + proxy)"
    }
  ]
}
```

`CMakeUserPresets.json` 已被 `.gitignore` 忽略，不应提交。

## 第三方依赖

均在 `cmake/Dependencies.cmake` 中通过 FetchContent 固定到发行 tag：

| 依赖 | 版本 | CMake 目标 | 说明 |
| --- | --- | --- | --- |
| cpp-httplib | v0.67.0 | `httplib::httplib` | Header-only；含 `httplib::Client` 与 `httplib::ws::WebSocketClient`；关闭 OpenSSL/Zlib/Brotli |
| nlohmann/json | v3.12.0 | `nlohmann_json::nlohmann_json` | Header-only |
| yaml-cpp | 0.8.0 | `yaml-cpp::yaml-cpp` | 静态库，关闭 tests/tools/contrib |
| doctest | v2.5.3 | `doctest::doctest` | 仅测试目标使用，不进入 GUI |

首次配置会克隆依赖源码；之后 `FETCHCONTENT_UPDATES_DISCONNECTED=ON` 会跳过远程更新检查。依赖源码位于构建目录的 `_deps/` 下。

## 构建

### Windows

在「x64 Native Tools Command Prompt for VS 2022」中，或先加载 VS 环境：

```powershell
& 'G:\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64

cmake --preset windows-x64-release
cmake --build --preset windows-x64-debug
cmake --build --preset windows-x64-release
```

输出：

- `out/build/windows-x64/bin/Debug/ClashYuka.exe`
- `out/build/windows-x64/bin/Release/ClashYuka.exe`

Visual Studio 以「打开文件夹」方式打开仓库即可，无需 `.sln`/`.vcxproj`。

### macOS

```bash
cmake --preset macos
cmake --build --preset macos-debug
cmake --build --preset macos-release
```

macOS 的 wxWidgets 通过系统或本地安装提供，需要保证 `find_package(wxWidgets)` 能找到，不复用 Windows 静态库。

### Linux

```bash
cmake --preset linux
cmake --build --preset linux-debug
cmake --build --preset linux-release
```

Linux 需安装 wxWidgets GTK 开发包（例如 `libwxgtk3.2-dev`）。

## 测试

```powershell
ctest --preset windows-x64-release --output-on-failure
ctest --preset windows-x64-debug --output-on-failure
```

测试覆盖：平台服务、运行时信息、JSON 解析、YAML 解析、httplib HTTP/WebSocket 客户端可用性、Mihomo 端点构造。

Visual Studio 的测试资源管理器可以发现问题（CTest 适配器），但当前环境通过 MCP 执行测试会返回失败且无诊断信息；请以 `ctest` 命令行为准。

## 本机代理（可选）

如果 GitHub 依赖拉取需要代理，可在 `CMakeUserPresets.json` 的 configure preset 中设置环境变量：

```json
"environment": {
  "HTTP_PROXY": "http://127.0.0.1:7890",
  "HTTPS_PROXY": "http://127.0.0.1:7890",
  "ALL_PROXY": "http://127.0.0.1:7890",
  "NO_PROXY": "localhost,127.0.0.1"
}
```

CMake Presets 的 `environment` 会传递给配置进程及其 git 子进程，Visual Studio 的 CMake 集成读取同一份用户预设。

## 常见问题

- **`WXWIDGETS_ROOT is not set`**：当前使用的 preset 没有本机 wxWidgets 路径。请使用基于 `CMakeUserPresets.json` 的 `windows-x64-*` 预设，或传 `-DWXWIDGETS_ROOT=...`。
- **`LNK2038: _ITERATOR_DEBUG_LEVEL 不匹配`**：Debug 配置必须保留 `_ITERATOR_DEBUG_LEVEL=0`，因为本机 wxWidgets 静态库是 Release 构建。
- **`LNK2038: RuntimeLibrary 不匹配`**：确认没有把 Debug 改成 `/MDd`；工程统一使用 `/MD`。
- **Ninja 报 `failed recompaction: Permission denied`**：通常是有另一个进程（例如 Visual Studio 的自动 CMake 配置）正在写同一构建目录。等待其结束后重试。
- **找不到 `cl.exe`**：命令行构建前需要进入 VS 开发者环境；Visual Studio 文件夹模式会自动提供。
