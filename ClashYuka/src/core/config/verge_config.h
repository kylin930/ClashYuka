// ClashYuka 应用配置（verge.yaml）数据结构定义
// 对应 Rust 版 config/nyanpasu/mod.rs 的 IVerge（含子模块 logging/widget/clash_strategy）
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace clash_yuka::config {

// 窗口持久化状态
struct WindowState {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    bool maximized = false;
    bool fullscreen = false;
};

// Clash 控制器端口选择策略
struct ClashStrategy {
    // fixed | random | allow_fallback
    std::string external_controller_port_strategy = "allow_fallback";
};

// verge.yaml schema
// 字段使用 std::optional 表示 "未设置"（YAML 中缺失）。字符串形式的枚举值
// 直接以 snake_case 存储（与 Rust serde rename_all 保持一致）。
struct IVerge {
    // 应用单例端口
    std::optional<uint16_t> app_singleton_port;
    // 应用日志级别：silent | trace | debug | info | warn | error
    std::optional<std::string> app_log_level;
    // 语言（i18n key）
    std::optional<std::string> language;
    // 主题：light | dark | system
    std::optional<std::string> theme_mode;
    // 是否显示流量图
    std::optional<bool> traffic_graph;
    // 是否显示内存占用（仅 mihomo）
    std::optional<bool> enable_memory_usage;
    // 全局动画效果
    std::optional<bool> lighten_animation_effects;
    // 是否启用 TUN 模式
    std::optional<bool> enable_tun_mode;
    // 是否启用服务模式（Windows）
    std::optional<bool> enable_service_mode;
    // 是否开机自启
    std::optional<bool> enable_auto_launch;
    // 是否静默启动
    std::optional<bool> enable_silent_start;
    // 是否设置系统代理
    std::optional<bool> enable_system_proxy;
    // 是否启用代理守卫
    std::optional<bool> enable_proxy_guard;
    // 系统代理 bypass
    std::optional<std::string> system_proxy_bypass;
    // 代理守卫间隔（秒）
    std::optional<uint64_t> proxy_guard_interval;
    // 主题色（#RRGGBB）
    std::optional<std::string> theme_color;
    // Web UI 列表
    std::optional<std::vector<std::string>> web_ui_list;
    // clash 核心：clash | clash-rs | mihomo | mihomo-alpha | clash-rs-alpha
    std::optional<std::string> clash_core;
    // 内核下载镜像源 URL（空表示直连 GitHub）
    std::optional<std::string> core_mirror_url;
    // 热键映射：["func,key", ...]
    std::optional<std::vector<std::string>> hotkeys;
    // 【弃用】切换代理时自动关闭连接 -> 已迁移到 break_when_proxy_change
    std::optional<bool> auto_close_connection;
    // 切换代理时中断连接：none | chain | all
    std::optional<std::string> break_when_proxy_change;
    // 切换配置时中断所有连接
    std::optional<bool> break_when_profile_change;
    // 切换模式时中断所有连接
    std::optional<bool> break_when_mode_change;
    // 默认延迟测试 URL
    std::optional<std::string> default_latency_test;
    // 是否启用 clash 字段过滤
    std::optional<bool> enable_clash_fields;
    // 是否启用内置增强脚本
    std::optional<bool> enable_builtin_enhanced;
    // 代理页面列数
    std::optional<int32_t> proxy_layout_column;
    // 【弃用】日志自动清理分钟数 -> 已迁移到 max_log_files
    std::optional<int64_t> auto_log_clean;
    // 日志轮转天数
    std::optional<size_t> max_log_files;
    // 【弃用】窗口位置 -> 已迁移到 window_size_state
    std::optional<std::vector<double>> window_size_position;
    // 窗口持久化状态
    std::optional<WindowState> window_size_state;
    // 是否启用随机端口
    std::optional<bool> enable_random_port;
    // 应用 mixed-port（覆盖 clash）
    std::optional<uint16_t> verge_mixed_port;
    // 启动时检查更新
    std::optional<bool> enable_auto_check_update;
    // Clash 控制器端口策略
    std::optional<ClashStrategy> clash_strategy;
    // 托盘代理选择模式：hidden | normal | submenu
    std::optional<std::string> clash_tray_selector;
    // 窗口置顶
    std::optional<bool> always_on_top;
    // TUN 堆栈：system | gvisor | mixed
    std::optional<std::string> tun_stack;
    // ---- mihomo 内存/性能调优（0/空表示使用 mihomo 或订阅默认值）----
    // GOGC 百分比（Go 运行时；默认 100，调低更省内存但更耗 CPU）
    std::optional<int32_t> mihomo_gogc;
    // GOMEMLIMIT（MiB，0 = 不限制）
    std::optional<int32_t> mihomo_memlimit_mb;
    // find-process-mode：off | strict | always（留空 = 跟随订阅）
    std::optional<std::string> mihomo_find_process_mode;
    // 覆盖 sniffer.enable（留空 = 跟随订阅）
    std::optional<bool> mihomo_sniffer_enable;
    // 网络统计浮窗：disabled | large | small
    std::optional<std::string> network_statistic_widget;
    // PAC URL
    std::optional<std::string> pac_url;
    // 是否启用托盘文字（Linux）
    std::optional<bool> enable_tray_text;
    // 窗口类型：main
    std::optional<std::string> window_type;
    // 托盘菜单模式：native | webview
    std::optional<std::string> tray_menu_mode;
    // 托盘菜单关闭行为：hide | close
    std::optional<std::string> tray_menu_close_behavior;

    // 默认模板（template()）
    static IVerge Template();

    // 从 verge.yaml 读取（缺失字段用模板默认；文件不存在返回 Template()）
    // 包含弃用字段迁移与 theme_color 校验。
    static IVerge Load(const std::filesystem::path& path);

    // 写回 verge.yaml
    void Save(const std::filesystem::path& path) const;

    // 局部更新：仅当 patch 中某字段被设置时覆盖到本对象
    void Patch(const IVerge& patch);
};

// 校验 #RRGGBB 颜色字符串
bool is_hex_color(const std::string& color);

} // namespace clash_yuka::config
