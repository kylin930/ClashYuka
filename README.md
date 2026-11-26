# ClashYuka

ClashYuka 是基于 wxWidgets 的 Windows 桌面客户端，通过 Mihomo（Clash.Meta）内核提供代理配置管理、节点选择、实时监控与系统代理控制。

## 功能

- **Dashboard**：实时上传/下载速率、累计流量、内存占用、活跃连接数。
- **Proxies**：代理组与节点列表、节点切换、单节点/整组延迟测试、Rule/Global/Direct 模式切换、名称搜索与排序。
- **Profiles**：远程订阅与本地配置管理，支持 Merge / Script 处理链，激活配置后自动重启内核并刷新界面。
- **Connections**：实时连接列表，支持按代理链筛选、搜索、排序与关闭连接。
- **Rules**：规则列表展示、按代理筛选与搜索。
- **Logs**：内核实时日志，支持级别筛选、搜索与自动滚动。
- **Providers**：代理/规则 provider 查看、更新与健康检查。
- **Settings**：系统代理与代理守卫、开机自启、主题与语言、内核与端口、TUN、热键、调试信息、关于页。

其他能力：系统托盘与静默启动、全局热键、系统代理守卫、配置迁移、内核下载与更新检查、Windows 服务模式。

## 目录结构

```text
ClashYuka/                  顶层 CMake 工程，负责 wxWidgets 与第三方依赖
├── ClashYuka/              可执行目标
│   ├── src/
│   │   ├── main.cpp
│   │   ├── core/           内核进程、API/WS 客户端、配置、托盘、系统代理、服务等
│   │   ├── service/        Windows 服务模式入口
│   │   └── ui/             主窗口、主题与各功能页面
│   ├── tests/              API 服务层测试与真实内核集成测试（默认关闭）
│   └── packaging/windows/  Inno Setup 脚本、版本资源与 manifest
├── CMakeLists.txt
└── CMakePresets.json
```

## 构建

环境要求：

- Windows 10/11，Visual Studio 2022（MSVC x64 工具链）
- CMake 3.20+ 与 Ninja
- wxWidgets 3.3 静态库（`/MT` 运行时）构建，默认路径为 `G:/Documents/wxwidgetsBuild`，可在顶层 `CMakeLists.txt` 的 `WX_ROOT` 中修改
- 首次配置需要网络访问 GitHub（FetchContent 拉取 nlohmann/json、yaml-cpp、mbedtls、IXWebSocket）

Release 构建：

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release
```

Debug 构建使用 `x64-debug` 预设。产物为 `out/build/<preset>/ClashYuka.exe`，构建完成后 `clashyuka-service.exe` 会自动复制到 `ClashYuka.exe` 同级的 `resources/` 目录。

运行前需将 Mihomo 内核（`mihomo.exe`）及 Geo 数据放入 `resources/` 目录，或通过设置页完成内核下载。

测试（默认关闭）：

```powershell
cmake --preset x64-release -DCLASHYUKA_BUILD_TESTS=ON
cmake --build out/build/x64-release
```

集成测试可通过环境变量 `YUKA_TEST_MIHOMO` 指定 mihomo 可执行文件。

## 运行时数据目录

| 内容 | 路径 |
| --- | --- |
| 配置（`verge.yaml`、`profiles.yaml`、`clash-guard-overrides.yaml`） | `%APPDATA%\ClashYuka` |
| 订阅、日志、缓存 | `%LOCALAPPDATA%\ClashYuka` |
| 内核与 Geo 资源 | `<exe 目录>\resources` |
| 开机自启注册表项 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ClashYuka` |
| Windows 服务名 | `ClashYuka Service` |
| TUN 设备名 | `YukaTun` |

## 许可

本项目以 GNU General Public License v3.0 发布，详见 [LICENSE](LICENSE)。
