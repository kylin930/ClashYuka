# ClashYuka

基于 Mihomo 的轻量级原生桌面代理客户端。当前完成 P0（工程基础）、P1（Mihomo Core 集成与通信层）、P2.0（MD3 自绘 UI 系统）与 P2.0.1（Material Motion 打磨）。

## 技术栈

- C++20 / CMake 3.31（Ninja Multi-Config）
- wxWidgets 3.3.1（Windows 使用本机静态库）
- cpp-httplib / nlohmann-json / yaml-cpp（FetchContent 固定版本）
- doctest（测试）

## 模块

| 模块 | 位置 | 说明 |
| --- | --- | --- |
| MD3 主题令牌 | `include/clashyuka/ui/theme` | 4 套配色 × Light/Dark、字体、圆角、间距、DPI |
| 绘制层 | `include/clashyuka/ui/drawing` | Painter、矢量图标、统一动画管理器、MotionTokens、Ripple |
| 组件库 | `include/clashyuka/ui/components` | Card/Button/IconButton/Switch/ListItem/Rail/Tab/Dialog/Snackbar/Progress |
| Demo | `include/clashyuka/ui/demo_frame.h`, `src/ui/demo_frame.cpp` | 组件展示与主题切换 |
| 数据模型与 JSON 解析 | `include/clashyuka/mihomo/types.h`, `src/mihomo/json.cpp` | 容错解析，未知字段/类型不匹配不会导致崩溃 |
| 统一错误 | `include/clashyuka/core/result.h` | `ErrorKind` + `Result<T>`，不向调用方暴露 httplib/nlohmann 异常 |
| REST 客户端 | `include/clashyuka/mihomo/api_client.h` | 每次请求独立连接，可跨线程使用 |
| WebSocket 客户端 | `include/clashyuka/mihomo/event_client.h` | 按需连接，后台线程，断线重连，安全退出 |
| 进程管理 | `include/clashyuka/mihomo/process.h` | 跨平台进程后端，捕获输出与退出码 |
| 平台适配 | `src/platform/{windows,posix}` | 进程启动/等待只在平台层使用系统 API |

### REST API

所有请求带 `Authorization: Bearer <secret>`；控制器默认 `127.0.0.1`，拒绝 `0.0.0.0`/`::` 等通配地址。

| 方法 | 路径 | 客户端方法 |
| --- | --- | --- |
| GET | `/version` | `GetVersion()` |
| GET | `/proxies` | `GetProxies()` |
| GET | `/proxies/{name}` | `GetProxy(name)` |
| PUT | `/proxies/{group}` | `SelectProxy(group, target)`，body `{"name": target}` |
| GET | `/configs` | `GetConfigs()` |
| PATCH | `/configs` | `PatchConfigs(patch)` |
| GET | `/rules` | `GetRules()` |
| GET | `/connections` | `GetConnections()` |
| GET | `/providers/proxies` | `GetProxyProviders()` |
| GET | `/providers/rules` | `GetRuleProviders()` |

节点/代理组名称按 RFC 3986 逐段百分号编码（空格、中文、`/` 都会被编码）。

错误分类：`InvalidConfig`、`ConnectionFailed`、`KernelNotRunning`、`Timeout`、`HttpStatus`、`AuthFailed`、`InvalidJson`、`MissingField`、`TypeMismatch`。

### WebSocket

Mihomo 的 `/logs`、`/traffic`、`/memory`、`/connections` 官方同时支持 `GET` 与 `WS`。流式读取只通过 WebSocket 实现：

- `ConnectTraffic`：默认必要的实时流量连接
- `ConnectLogs(handler, level)`
- `ConnectConnections(handler, intervalMs)`
- `ConnectMemory`
- `Disconnect(channel)` / `Disconnect()`

所有回调在后台线程执行，GUI 必须通过 `wxTheApp->CallAfter` 或 `wxQueueEvent` 切回主线程。默认不自动连接任何通道；实例析构会断开并 join 所有工作线程。

### 进程管理

`MihomoProcess` 支持 Start/Stop/Restart/IsRunning/GetPid/GetExitCode，捕获 stdout/stderr 行，并通过监控线程上报退出码。内核路径与参数（如 `-f config.yaml -d home`）由调用方显式提供，不自动下载或替换内核。

`wxProcess` 已评估，但其退出通知依赖 GUI 消息泵，无法在无界面验证和关闭时序中可靠工作；因此启动/等待放在 `src/platform` 后端中实现，公共模块不出现 `CreateProcess`/`fork`。

Windows 停止流程：GUI 进程无控制台时先 `AttachConsole` 并向子进程组发送 `CTRL_BREAK_EVENT`，等待 3 秒后才 `TerminateProcess`；测试进程自带控制台，因此自动化测试只验证强制终止路径。

## MD3 设计系统

- 主题：Purple / Blue / Teal / Orange，每套都有完整 Light/Dark 颜色令牌
- 切换主题与深浅色无需重启，组件通过 `ThemeManager` 观察者即时重绘
- 动画由单个 `wxTimer`（16ms）驱动的 `AnimationManager` 统一调度，组件不自建定时器
- 所有尺寸通过 `wxWindow::FromDIP` 缩放；不直接使用物理像素
- 自定义图标为 24×24 网格矢量描边，无外部图标资源

### 动效（P2.0.1）

时长与曲线集中在 `include/clashyuka/ui/drawing/motion_tokens.h`，组件内不写魔法数字：

| 动效 | 时长 |
| --- | --- |
| Hover / Disabled 颜色 | 140ms |
| Press / Release | 110 / 130ms |
| Focus 状态层 | 120ms |
| Switch、Tab、NavigationRail | 180 / 200 / 200ms |
| Surface / Dialog / Snackbar 进入 | 200ms |
| Surface 退出 | 160ms |
| 页面切换 | 180ms |
| Ripple | 420ms |
| 进度插值 | 220ms |

曲线：`Linear`、`Standard (0.2, 0, 0, 1)`、`EmphasizedDecelerate`、`EmphasizedAccelerate`、`EaseOutCubic`、`EaseInOutCubic`，贝塞尔曲线内置求解，无外部依赖。

- Button / IconButton：Hover、Press 缩放、按点击位置发起的 Ripple（圆角裁剪）、键盘 Focus 状态层（无描边焦点框）、Disabled 过渡
- Switch：Thumb 位置与尺寸、Track 颜色同步动画
- ListItem：Hover / Pressed / 选中态背景与前景色过渡、按点击位置发起的 Ripple（圆角裁剪），不改变布局
- SurfaceCard：Hover / Pressed 背景与描边过渡
- NavigationRail / TabBar：指示器平滑移动、宽度变化、文字颜色交叉渐变
- Dialog：无系统标题栏的 MD3 圆角窗口（`SetShape` 裁剪）、自绘标题行与关闭图标、标题区可拖动；整窗淡入 + 内容轻微上移，关闭/ESC 时反向淡出下滑后再结束模态循环（Windows 分层窗口；其他平台保持不透明）；Snackbar：滑入淡入、可复用同一条消息实例
- ProgressIndicator：确定进度插值并带完成脉冲；不确定进度连续旋转
- 页面切换：新页面轻微上滑进入，快速连续切换会重置上一段动画

关闭动画或开启 Reduced Motion 时，所有动画立即到达最终状态，交互与状态仍然可用；不执行 Ripple、缩放、位移与旋转。

Demo 窗口（`ClashYuka.exe`）快捷键：

| 快捷键 | 作用 |
| --- | --- |
| F1 / F2 / F3 / F4 / F5 | Overview / Components / Feedback / Motion / About |
| Ctrl+1..4 | Purple / Blue / Teal / Orange |
| Ctrl+D | 切换深色模式 |
| Ctrl+M / Ctrl+R | 开关动画 / Reduced Motion |
| Ctrl+T | 切换组件页 Tab |
| Ctrl+S | 显示 Snackbar |
| Ctrl+E | 显示 Dialog |

Motion 页面提供动画开关、Reduced Motion、按钮/图标按钮按压、Switch、进度与 Snackbar 的直接演示。

界面截图位于 `docs/screenshots/`：主题（`theme-*.png`）、Dialog、Snackbar、Motion 页（`motion-page.png`、`release-motion.png`）、动画关闭（`animations-off.png`）、Reduced Motion（`reduced-motion.png`）、组件状态（`button-ripple.png`、`switch-states.png`、`rail-selected.png`、`tab-indicator.png`）。

## 平台状态

| 平台 | 状态 |
| --- | --- |
| Windows 10/11 x64 (MSVC) | 已构建 Debug/Release，单元/Mock/真实内核集成测试通过 |
| macOS | 仅 CMake Preset，未构建验证（POSIX 进程后端已提供） |
| Linux | 仅 CMake Preset，未构建验证（POSIX 进程后端已提供） |

## 构建

### Windows（CLI）

CLI 与 Visual Studio 使用**不同**构建目录，避免同时写同一个 binaryDir：

- CLI：`out/build/windows-x64`
- Visual Studio：`out/build/vs/windows-x64`（Preset：`vs-windows-x64`）

```powershell
& 'G:\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64

cmake --preset windows-x64-release
cmake --build --preset windows-x64-debug
cmake --build --preset windows-x64-release
```

输出：`out/build/windows-x64/bin/{Debug,Release}/ClashYuka.exe`。

Visual Studio 以「打开文件夹」方式打开仓库，在配置下拉框中选择 `Visual Studio (separate build dir)`。不需要 `.sln`/`.vcxproj`。

### macOS / Linux

```bash
cmake --preset macos    # 或 linux
cmake --build --preset macos-debug
cmake --build --preset macos-release
```

需要系统安装 wxWidgets，不复用 Windows 静态库。

## 测试

```powershell
ctest --preset windows-x64-release --output-on-failure
ctest --preset windows-x64-debug --output-on-failure
```

包含三个 CTest 目标：

- `clashyuka_tests`：数据模型/JSON、URL 编码、Mock HTTP（鉴权、PUT、错误、超时）、Mock WebSocket（收发、异常消息、主动断开、重连、析构清理）
- `clashyuka_ui_tests`：主题/DPI/形状令牌、缓动函数端点与单调性、Bezier 插值、动画中途改目标、动画取消与 Handle 析构、多动画并行
- `clashyuka_process_tests`：进程启动、输出捕获、退出码、重复启动/停止、重启、非法路径

真实内核验证不注册进 CTest（需要外部 mihomo）：

```powershell
out\build\windows-x64\bin\Release\clashyuka_mihomo_integration.exe "C:\path\to\mihomo.exe"
```

它会生成随机端口与随机 Secret 的临时配置，验证版本/代理组/节点切换/流量与日志 WebSocket/停止与无孤儿进程，结束后删除临时目录。

Visual Studio 的测试资源管理器可以发现问题，但当前环境下 MCP `test_run` 返回失败且无诊断，测试以 `ctest` 命令行为准。

## wxWidgets 静态库（Windows）

- `G:/Documents/wxwidgetsBuild`，库目录 `lib/vc_x64_lib`
- 3.3.1，Unicode，x64，仅 Release 静态库，CRT `/MD`
- 全配置统一 `/MD`，Debug 额外 `_ITERATOR_DEBUG_LEVEL=0`
- 不要在 Debug 使用 `/MDd`

个人路径与代理只写在 `CMakeUserPresets.json`（已 gitignore）：

```json
{
  "configurePresets": [
    {
      "name": "windows-x64-release",
      "inherits": "windows-cli-local"
    }
  ]
}
```

## 第三方依赖

| 依赖 | 版本 | 目标 |
| --- | --- | --- |
| cpp-httplib | v0.67.0 | `httplib::httplib` |
| nlohmann/json | v3.12.0 | `nlohmann_json::nlohmann_json` |
| yaml-cpp | 0.8.0 | `yaml-cpp::yaml-cpp` |
| doctest | v2.5.3 | `doctest::doctest`（仅测试） |

## 安全约定

- 测试配置与 Secret 只在系统临时目录生成，测试结束删除。
- Secret 由 `std::random_device` 生成，不写日志、不进 Git。
- 真实内核测试只连接 `127.0.0.1`，随机端口，`allow-lan: false`。
- 不修改用户现有订阅或配置。

## 常见问题

- **`WXWIDGETS_ROOT is not set`**：使用基于 `CMakeUserPresets.json` 的 `windows-x64-*` 或 `vs-windows-x64` 预设。
- **`LNK2038`**：保持 `/MD` 与 `_ITERATOR_DEBUG_LEVEL=0`。
- **Ninja `failed recompaction`**：CLI 与 VS 不要同时配置同一个 binaryDir；本项目已拆分目录。
- **Real integration 启动失败**：确认 mihomo 二进制与 CPU 指令集兼容（`amd64-v3` 需要 AVX2）。
