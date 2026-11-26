// ClashYuka 应用配置（verge.yaml）读写实现
#include "verge_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace clash_yuka::config {

namespace {

// ---- yaml-cpp 与 std::optional 之间的小工具 ----

template <typename T>
void read_opt(const YAML::Node& node, const char* key, std::optional<T>& out) {
    if (!node) return;
    if (auto n = node[key]; n && !n.IsNull()) {
        try {
            out = n.as<T>();
        } catch (...) {
            // 类型不匹配时保留原值（默认）
        }
    }
}

void read_opt_string(const YAML::Node& node, const char* key, std::optional<std::string>& out) {
    if (!node) return;
    if (auto n = node[key]; n && !n.IsNull()) {
        try {
            out = n.as<std::string>();
        } catch (...) {
            // 可能是 bool/数字，转字符串
            try { out = n.as<bool>() ? "true" : "false"; } catch (...) {}
        }
    }
}

template <typename T>
void write_opt(YAML::Node& node, const char* key, const std::optional<T>& val) {
    if (val.has_value()) {
        node[key] = *val;
    }
}

// WindowState 读写
WindowState read_window_state(const YAML::Node& n) {
    WindowState s;
    if (auto w = n["width"]; w) s.width = w.as<uint32_t>(0);
    if (auto h = n["height"]; h) s.height = h.as<uint32_t>(0);
    if (auto x = n["x"]; x) s.x = x.as<int32_t>(0);
    if (auto y = n["y"]; y) s.y = y.as<int32_t>(0);
    if (auto m = n["maximized"]; m) s.maximized = m.as<bool>(false);
    if (auto f = n["fullscreen"]; f) s.fullscreen = f.as<bool>(false);
    return s;
}

YAML::Node write_window_state(const WindowState& s) {
    YAML::Node n;
    n["width"] = s.width;
    n["height"] = s.height;
    n["x"] = s.x;
    n["y"] = s.y;
    n["maximized"] = s.maximized;
    n["fullscreen"] = s.fullscreen;
    return n;
}

ClashStrategy read_clash_strategy(const YAML::Node& n) {
    ClashStrategy s;
    if (auto v = n["external_controller_port_strategy"]; v) {
        s.external_controller_port_strategy = v.as<std::string>("allow_fallback");
    }
    return s;
}

YAML::Node write_clash_strategy(const ClashStrategy& s) {
    YAML::Node n;
    n["external_controller_port_strategy"] = s.external_controller_port_strategy;
    return n;
}

} // namespace

bool is_hex_color(const std::string& color) {
    if (color.size() != 7 || color[0] != '#') return false;
    for (size_t i = 1; i < color.size(); ++i) {
        char c = color[i];
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

IVerge IVerge::Template() {
    IVerge v;
    v.clash_core = "mihomo";
    v.core_mirror_url = "";
    v.language = "en";
    v.app_log_level = "info";
    v.theme_mode = "system";
    v.traffic_graph = true;
    v.enable_memory_usage = true;
    v.enable_auto_launch = false;
    v.enable_silent_start = false;
    v.enable_system_proxy = false;
    v.enable_random_port = false;
    v.verge_mixed_port = 7890;
    // mihomo 内存/性能调优默认值：0 = 使用 mihomo 默认（GOGC=100 / 不限制）
    v.mihomo_gogc = 0;
    v.mihomo_memlimit_mb = 0;
    v.enable_proxy_guard = false;
    v.proxy_guard_interval = 30;
    v.break_when_proxy_change = "all";
    v.break_when_profile_change = true;
    v.break_when_mode_change = true;
    v.enable_builtin_enhanced = true;
    v.enable_clash_fields = true;
    v.lighten_animation_effects = false;
    v.max_log_files = 7;
    v.enable_auto_check_update = true;
    v.clash_tray_selector = "normal";
    v.enable_service_mode = false;
    v.always_on_top = false;
    v.enable_tray_text = false;
    v.window_type = "main";
#ifdef _WIN32
    v.tray_menu_mode = "webview";
#else
    v.tray_menu_mode = "native";
#endif
    v.tray_menu_close_behavior = "hide";
    return v;
}

IVerge IVerge::Load(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return Template();
    }
    YAML::Node root;
    try {
        root = YAML::Load(ifs);
    } catch (...) {
        return Template();
    }
    if (!root || !root.IsMap()) {
        return Template();
    }

    IVerge v;

    read_opt(root, "app_singleton_port", v.app_singleton_port);
    read_opt_string(root, "app_log_level", v.app_log_level);
    read_opt_string(root, "language", v.language);
    read_opt_string(root, "theme_mode", v.theme_mode);
    read_opt(root, "traffic_graph", v.traffic_graph);
    read_opt(root, "enable_memory_usage", v.enable_memory_usage);
    read_opt(root, "lighten_animation_effects", v.lighten_animation_effects);
    read_opt(root, "enable_tun_mode", v.enable_tun_mode);
    read_opt(root, "enable_service_mode", v.enable_service_mode);
    read_opt(root, "enable_auto_launch", v.enable_auto_launch);
    read_opt(root, "enable_silent_start", v.enable_silent_start);
    read_opt(root, "enable_system_proxy", v.enable_system_proxy);
    read_opt(root, "enable_proxy_guard", v.enable_proxy_guard);
    read_opt_string(root, "system_proxy_bypass", v.system_proxy_bypass);
    read_opt(root, "proxy_guard_interval", v.proxy_guard_interval);
    // proxy_guard_duration 是旧别名
    if (!v.proxy_guard_interval.has_value()) {
        read_opt(root, "proxy_guard_duration", v.proxy_guard_interval);
    }
    read_opt_string(root, "theme_color", v.theme_color);
    if (auto n = root["web_ui_list"]; n && n.IsSequence()) {
        std::vector<std::string> lst;
        for (auto it = n.begin(); it != n.end(); ++it) {
            lst.push_back(it->as<std::string>());
        }
        v.web_ui_list = std::move(lst);
    }
    read_opt_string(root, "clash_core", v.clash_core);
    read_opt_string(root, "core_mirror_url", v.core_mirror_url);
    if (auto n = root["hotkeys"]; n && n.IsSequence()) {
        std::vector<std::string> lst;
        for (auto it = n.begin(); it != n.end(); ++it) {
            lst.push_back(it->as<std::string>());
        }
        v.hotkeys = std::move(lst);
    }
    read_opt(root, "auto_close_connection", v.auto_close_connection);
    read_opt_string(root, "break_when_proxy_change", v.break_when_proxy_change);
    read_opt(root, "break_when_profile_change", v.break_when_profile_change);
    read_opt(root, "break_when_mode_change", v.break_when_mode_change);
    read_opt_string(root, "default_latency_test", v.default_latency_test);
    read_opt(root, "enable_clash_fields", v.enable_clash_fields);
    read_opt(root, "enable_builtin_enhanced", v.enable_builtin_enhanced);
    read_opt(root, "proxy_layout_column", v.proxy_layout_column);
    read_opt(root, "auto_log_clean", v.auto_log_clean);
    read_opt(root, "max_log_files", v.max_log_files);
    if (auto n = root["window_size_position"]; n && n.IsSequence()) {
        std::vector<double> lst;
        for (auto it = n.begin(); it != n.end(); ++it) {
            lst.push_back(it->as<double>());
        }
        v.window_size_position = std::move(lst);
    }
    if (auto n = root["window_size_state"]; n && n.IsMap()) {
        v.window_size_state = read_window_state(n);
    }
    read_opt(root, "enable_random_port", v.enable_random_port);
    read_opt(root, "verge_mixed_port", v.verge_mixed_port);
    read_opt(root, "enable_auto_check_update", v.enable_auto_check_update);
    if (auto n = root["clash_strategy"]; n && n.IsMap()) {
        v.clash_strategy = read_clash_strategy(n);
    }
    read_opt_string(root, "clash_tray_selector", v.clash_tray_selector);
    read_opt(root, "always_on_top", v.always_on_top);
    read_opt_string(root, "tun_stack", v.tun_stack);
    read_opt(root, "mihomo_gogc", v.mihomo_gogc);
    read_opt(root, "mihomo_memlimit_mb", v.mihomo_memlimit_mb);
    read_opt_string(root, "mihomo_find_process_mode", v.mihomo_find_process_mode);
    read_opt(root, "mihomo_sniffer_enable", v.mihomo_sniffer_enable);
    read_opt_string(root, "network_statistic_widget", v.network_statistic_widget);
    read_opt_string(root, "pac_url", v.pac_url);
    read_opt(root, "enable_tray_text", v.enable_tray_text);
    read_opt_string(root, "window_type", v.window_type);
    read_opt_string(root, "tray_menu_mode", v.tray_menu_mode);
    read_opt_string(root, "tray_menu_close_behavior", v.tray_menu_close_behavior);

    // ---- 校验 theme_color ----
    if (v.theme_color) {
        const auto& c = *v.theme_color;
        if (!c.empty() && !is_hex_color(c)) {
            v.theme_color.reset();
        }
    }

    // ---- 弃用字段迁移：auto_close_connection -> break_when_proxy_change ----
    if (v.auto_close_connection && !v.break_when_proxy_change) {
        v.break_when_proxy_change = *v.auto_close_connection ? std::string("all") : std::string("none");
    }
    // auto_log_clean / window_size_position 暂不做强制迁移（保留读取以便回滚）

    // ---- 合并模板默认值 ----
    IVerge t = Template();
    auto merge_str = [&](std::optional<std::string>& dst, const std::optional<std::string>& src) {
        if (!dst.has_value() && src.has_value()) dst = src;
    };
    auto merge_bool = [&](std::optional<bool>& dst, const std::optional<bool>& src) {
        if (!dst.has_value() && src.has_value()) dst = src;
    };
    auto merge_num = [&](auto& dst, const auto& src) {
        if (!dst.has_value() && src.has_value()) dst = src;
    };

    merge_bool(v.enable_auto_check_update, t.enable_auto_check_update);
    merge_str(v.clash_tray_selector, t.clash_tray_selector);
    merge_num(v.max_log_files, t.max_log_files);
    merge_bool(v.lighten_animation_effects, t.lighten_animation_effects);
    merge_bool(v.enable_service_mode, t.enable_service_mode);
    merge_str(v.break_when_proxy_change, t.break_when_proxy_change);
    merge_bool(v.break_when_profile_change, t.break_when_profile_change);
    merge_bool(v.break_when_mode_change, t.break_when_mode_change);
    merge_bool(v.enable_tray_text, t.enable_tray_text);
    merge_str(v.window_type, t.window_type);
    merge_str(v.tray_menu_mode, t.tray_menu_mode);
    merge_str(v.tray_menu_close_behavior, t.tray_menu_close_behavior);
    merge_num(v.mihomo_gogc, t.mihomo_gogc);
    merge_num(v.mihomo_memlimit_mb, t.mihomo_memlimit_mb);
    merge_str(v.mihomo_find_process_mode, t.mihomo_find_process_mode);
    merge_bool(v.mihomo_sniffer_enable, t.mihomo_sniffer_enable);

    return v;
}

void IVerge::Save(const std::filesystem::path& path) const {
    YAML::Node root;
    write_opt(root, "app_singleton_port", app_singleton_port);
    write_opt(root, "app_log_level", app_log_level);
    write_opt(root, "language", language);
    write_opt(root, "theme_mode", theme_mode);
    write_opt(root, "traffic_graph", traffic_graph);
    write_opt(root, "enable_memory_usage", enable_memory_usage);
    write_opt(root, "lighten_animation_effects", lighten_animation_effects);
    write_opt(root, "enable_tun_mode", enable_tun_mode);
    write_opt(root, "tun_stack", tun_stack);
    write_opt(root, "mihomo_gogc", mihomo_gogc);
    write_opt(root, "mihomo_memlimit_mb", mihomo_memlimit_mb);
    write_opt(root, "mihomo_find_process_mode", mihomo_find_process_mode);
    write_opt(root, "mihomo_sniffer_enable", mihomo_sniffer_enable);
    write_opt(root, "enable_service_mode", enable_service_mode);
    write_opt(root, "enable_auto_launch", enable_auto_launch);
    write_opt(root, "enable_silent_start", enable_silent_start);
    write_opt(root, "enable_system_proxy", enable_system_proxy);
    write_opt(root, "enable_proxy_guard", enable_proxy_guard);
    write_opt(root, "system_proxy_bypass", system_proxy_bypass);
    write_opt(root, "proxy_guard_interval", proxy_guard_interval);
    write_opt(root, "theme_color", theme_color);
    write_opt(root, "web_ui_list", web_ui_list);
    write_opt(root, "clash_core", clash_core);
    write_opt(root, "core_mirror_url", core_mirror_url);
    write_opt(root, "hotkeys", hotkeys);
    // 注意：auto_close_connection 已弃用，写回时跳过
    write_opt(root, "break_when_proxy_change", break_when_proxy_change);
    write_opt(root, "break_when_profile_change", break_when_profile_change);
    write_opt(root, "break_when_mode_change", break_when_mode_change);
    write_opt(root, "default_latency_test", default_latency_test);
    write_opt(root, "enable_clash_fields", enable_clash_fields);
    write_opt(root, "enable_builtin_enhanced", enable_builtin_enhanced);
    write_opt(root, "proxy_layout_column", proxy_layout_column);
    // 注意：auto_log_clean 已弃用，写回时跳过
    write_opt(root, "max_log_files", max_log_files);
    // 注意：window_size_position 已弃用，写回时跳过
    if (window_size_state) {
        root["window_size_state"] = write_window_state(*window_size_state);
    }
    write_opt(root, "enable_random_port", enable_random_port);
    write_opt(root, "verge_mixed_port", verge_mixed_port);
    write_opt(root, "enable_auto_check_update", enable_auto_check_update);
    if (clash_strategy) {
        root["clash_strategy"] = write_clash_strategy(*clash_strategy);
    }
    write_opt(root, "clash_tray_selector", clash_tray_selector);
    write_opt(root, "always_on_top", always_on_top);
    write_opt(root, "tun_stack", tun_stack);
    write_opt(root, "network_statistic_widget", network_statistic_widget);
    write_opt(root, "pac_url", pac_url);
    write_opt(root, "enable_tray_text", enable_tray_text);
    write_opt(root, "window_type", window_type);
    write_opt(root, "tray_menu_mode", tray_menu_mode);
    write_opt(root, "tray_menu_close_behavior", tray_menu_close_behavior);

    // 输出（带文件头注释）
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return;
    ofs << "# ClashYuka Config\n\n";
    ofs << YAML::Dump(root);
}

void IVerge::Patch(const IVerge& patch) {
#define PATCH_FIELD(name) if (patch.name) name = patch.name
    PATCH_FIELD(app_singleton_port);
    PATCH_FIELD(app_log_level);
    PATCH_FIELD(language);
    PATCH_FIELD(theme_mode);
    PATCH_FIELD(traffic_graph);
    PATCH_FIELD(enable_memory_usage);
    PATCH_FIELD(lighten_animation_effects);
    PATCH_FIELD(enable_tun_mode);
    PATCH_FIELD(enable_service_mode);
    PATCH_FIELD(enable_auto_launch);
    PATCH_FIELD(enable_silent_start);
    PATCH_FIELD(enable_system_proxy);
    PATCH_FIELD(enable_proxy_guard);
    PATCH_FIELD(system_proxy_bypass);
    PATCH_FIELD(proxy_guard_interval);
    PATCH_FIELD(theme_color);
    PATCH_FIELD(web_ui_list);
    PATCH_FIELD(clash_core);
    PATCH_FIELD(core_mirror_url);
    PATCH_FIELD(hotkeys);
    // 不再 patch 已弃用字段
    PATCH_FIELD(break_when_proxy_change);
    PATCH_FIELD(break_when_profile_change);
    PATCH_FIELD(break_when_mode_change);
    PATCH_FIELD(default_latency_test);
    PATCH_FIELD(enable_clash_fields);
    PATCH_FIELD(enable_builtin_enhanced);
    PATCH_FIELD(proxy_layout_column);
    PATCH_FIELD(max_log_files);
    PATCH_FIELD(window_size_state);
    PATCH_FIELD(enable_random_port);
    PATCH_FIELD(verge_mixed_port);
    PATCH_FIELD(enable_auto_check_update);
    PATCH_FIELD(clash_strategy);
    PATCH_FIELD(clash_tray_selector);
    PATCH_FIELD(always_on_top);
    PATCH_FIELD(tun_stack);
    PATCH_FIELD(mihomo_gogc);
    PATCH_FIELD(mihomo_memlimit_mb);
    PATCH_FIELD(mihomo_find_process_mode);
    PATCH_FIELD(mihomo_sniffer_enable);
    PATCH_FIELD(network_statistic_widget);
    PATCH_FIELD(pac_url);
    PATCH_FIELD(enable_tray_text);
    PATCH_FIELD(window_type);
    PATCH_FIELD(tray_menu_mode);
    PATCH_FIELD(tray_menu_close_behavior);
#undef PATCH_FIELD
}

} // namespace clash_yuka::config
