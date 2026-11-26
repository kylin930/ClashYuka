# Mihomo Geo 数据下载源预设

## 背景

目前设置页不能选择 Geo 下载源。导入或订阅的 YAML 可以自带 `geox-url`；未设置时由 Mihomo 选用默认地址。订阅能下载并不代表 Geo 数据也能下载，缺少 Geo 数据时，内核预检可能在下载阶段超时。

## 实施

1. 在 Settings → Mihomo 中增加 `Geo data download source` 下拉框，沿用现有保存与重启提示。提供「沿用配置（默认）」「MetaCubeX · GitHub Release」「MetaCubeX · GitHub Raw」「MetaCubeX · jsDelivr」。后三项是同一上游的不同渠道，不承诺缓存与更新时间完全一致。仅持久化稳定预设 ID；旧设置缺字段或遇到未知 ID 时只回退该字段。
2. 所有渠道统一映射 `geoip → geoip.dat`、`geosite → geosite.dat`、`mmdb → geoip.metadb`、`asn → GeoLite2-ASN.mmdb`。使用以下 HTTPS 根地址：
   - `https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/`
   - `https://raw.githubusercontent.com/MetaCubeX/meta-rules-dat/release/`
   - `https://cdn.jsdelivr.net/gh/MetaCubeX/meta-rules-dat@release/`
   - [Release 清单](https://api.github.com/repos/MetaCubeX/meta-rules-dat/releases/tags/latest)列出了四个资源。[Raw 分支清单](https://api.github.com/repos/MetaCubeX/meta-rules-dat/contents?ref=release)与 [jsDelivr 清单](https://data.jsdelivr.com/v1/package/gh/MetaCubeX/meta-rules-dat@release/flat)作为交叉核对；本机 HEAD 检查 Raw 和 jsDelivr 四项均返回 HTTP 200，Release 两项返回 200、另两项仅观察到 302。尚未验证所有渠道的完整文件可被 Mihomo 加载。
3. 从 [settings_page.cpp](file:///Z:/projects/ClashYuka/src/ui/pages/settings_page.cpp#L179) 的现有设置保存流程，经 [settings_store.cpp](file:///Z:/projects/ClashYuka/src/runtime/settings_store.cpp) 与 [BuildKernelOptions](file:///Z:/projects/ClashYuka/src/app/app_controller.cpp#L343) 传递到 [PrepareLaunch](file:///Z:/projects/ClashYuka/src/runtime/kernel_session.cpp#L547)。在 [runtime_config.cpp](file:///Z:/projects/ClashYuka/src/runtime/runtime_config.cpp#L386) 的两条运行副本路径共用结构化 YAML 覆盖：默认不注入，保留现有行为；显式选择仅覆盖 `geox-url` 四个键，保留其他设置，不修改订阅原文或因换源重写 legacy 文件。不改变同一次操作中内置模板其他设置的现有生成规则。
4. 保存后在下次启动、重启或切换生成新运行副本时生效，不清缓存、不强制重下。保留现有预检、启动复用和回滚机制，不修改真实系统代理。

## 验证

- 扩展 [test_runtime.cpp](file:///Z:/projects/ClashYuka/tests/test_runtime.cpp)：设置往返、旧设置缺字段与未知 ID、两条副本路径的默认兼容与四键覆盖、源文件与已有缓存不因换源修改、预检和启动复用副本、失败时保留或恢复旧会话。
- 用 VS CMake preset 构建并执行 CTest；使用临时数据目录与独立端口做实际 Mihomo 下载验证，区分资源发布、可达和完整加载。不将外网依赖加入常规 CTest，也不关闭用户正在运行的实例。
