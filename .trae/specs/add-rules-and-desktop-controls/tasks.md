# 实施任务

- [x] 1. 优先修复 Mihomo ZIP 归档识别。核实截图所示发布包的条目命名。覆盖旧命名、平台版本命名、无匹配、重复候选、损坏归档和失败后保留原内核。
- [x] 2. 接入规则页与规则提供者刷新。复用现有 Mihomo 规则查询和会话，增加按名称更新、刷新后读取、搜索、独立状态和会话隔离测试。
- [x] 3. 接入原生托盘、关闭到托盘设置和 Windows 可配置全局快捷键。验证显隐、注册冲突、退出注销和既有系统代理恢复路径。
- [x] 4. 在设置页加入 ClashYuka 自身更新检查和官方发布入口。确认官方发布源和可用资产，以替身覆盖版本判断、无资产、请求失败和取消。
- [x] 5. 使用现有 VS MCP 与 CMake 预设验证 Windows Debug、Release 构建及相关回归。在隔离数据目录使用 Mihomo 验证规则流程。记录托盘、快捷键及真实发布下载的实测范围。

- [x] 6. 修复验证缺陷：真实包 `mihomo-windows-amd64-compatible-v1.19.34.zip` 内条目为无版本后缀的 `mihomo-windows-amd64-compatible.exe`，现有识别仍报「does not contain the Mihomo executable」。扩展识别并复跑安装器测试与 Debug、Release 全量回归。

验证记录：真实 ZIP 条目为 `mihomo-windows-amd64-compatible.exe`，SHA256 `5a0231811a80b27477ebdee15684b41946d01afbd2114534e9a4b256125ca147`；安装器测试 14/14 通过，含该真实包的哈希校验。Debug、Release 全量 CTest 各 9/9。规则页与提供者刷新已用隔离 Mihomo v1.19.34 验证规则顺序、提供者计数与刷新。托盘交互、全局快捷键实按、真实发布源下载未实测（无发布源，且按用户要求不使用 UI 自动化）。

任务 1 独立且优先。任务 2、3、4 的基础模块可独立进行，共享 AppController 和设置页接入时协调修改，最后统一验收。
