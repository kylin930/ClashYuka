// ClashYuka 国际化管理实现
#include "core/i18n.h"

namespace clash_yuka::core {

I18n& I18n::Get() {
    static I18n instance;
    return instance;
}

I18n::I18n() {
    InitTables();
}

void I18n::InitTables() {
    // 辅助 lambda：添加一条翻译（简体 / 繁体 / 英文）
    auto add = [this](const std::string& key,
                      const std::string& zh_cn,
                      const std::string& zh_tw,
                      const std::string& en) {
        table_[key]["zh-cn"] = wxString::FromUTF8(zh_cn);
        table_[key]["zh-tw"] = wxString::FromUTF8(zh_tw);
        table_[key]["en"] = wxString::FromUTF8(en);
    };

    // ---- 导航项（8 项）----
    add("nav.dashboard",   "仪表盘", "儀表盤", "Dashboard");
    add("nav.proxies",     "代理",   "代理",   "Proxies");
    add("nav.profiles",    "订阅",   "訂閱",   "Profiles");
    add("nav.connections", "连接",   "連接",   "Connections");
    add("nav.rules",       "规则",   "規則",   "Rules");
    add("nav.logs",        "日志",   "日誌",   "Logs");
    add("nav.providers",   "提供者", "提供者", "Providers");
    add("nav.settings",    "设置",   "設定",   "Settings");

    // ---- 通用按钮 ----
    add("button.save",       "保存", "儲存",     "Save");
    add("button.delete",     "删除", "刪除",     "Delete");
    add("button.cancel",     "取消", "取消",     "Cancel");
    add("button.confirm",    "确认", "確認",     "Confirm");
    add("button.import",     "导入", "匯入",     "Import");
    add("button.new",        "新建", "新建",     "New");
    add("button.refresh",    "刷新", "重新整理", "Refresh");
    add("button.activate",   "激活", "啟用",     "Activate");
    add("button.copy",       "复制", "複製",     "Copy");
    add("button.edit",       "编辑", "編輯",     "Edit");
    add("button.search",     "搜索", "搜尋",     "Search");
    add("button.close",      "关闭", "關閉",     "Close");
    add("button.ok",         "确定", "確定",     "OK");
    add("button.yes",        "是",   "是",       "Yes");
    add("button.no",         "否",   "否",       "No");
    add("button.retry",      "重试", "重試",     "Retry");
    add("button.apply",      "应用", "套用",     "Apply");
    add("button.reset",      "重置", "重設",     "Reset");
    add("button.update",     "更新", "更新",     "Update");
    add("button.download",   "下载", "下載",     "Download");
    add("button.select_all", "全选", "全選",     "Select All");

    // ---- 设置页子页名称（7 项）----
    add("settings.general",     "通用",       "一般",       "General");
    add("settings.clash",       "Clash 核心", "Clash 核心", "Clash Core");
    add("settings.system",      "系统",       "系統",       "System");
    add("settings.proxy_guard", "代理守卫",   "代理守衛",   "Proxy Guard");
    add("settings.hotkey",      "快捷键",     "快捷鍵",     "Hotkeys");
    add("settings.appearance",  "外观",       "外觀",       "Appearance");
    add("settings.about",       "关于",       "關於",       "About");

    // ---- 设置项标签 ----
    add("settings.language",                "语言",           "語言",             "Language");
    add("settings.theme_mode",              "主题模式",       "主題模式",         "Theme Mode");
    add("settings.theme_color",             "主题色",         "主題色",           "Theme Color");
    add("settings.app_log_level",           "日志级别",       "日誌級別",         "Log Level");
    add("settings.traffic_graph",           "流量图",         "流量圖",           "Traffic Graph");
    add("settings.enable_memory_usage",     "显示内存占用",   "顯示記憶體使用量", "Show Memory Usage");
    add("settings.enable_auto_launch",      "开机自启",       "開機自啟",         "Launch at Startup");
    add("settings.enable_silent_start",     "静默启动",       "靜默啟動",         "Silent Start");
    add("settings.enable_system_proxy",     "系统代理",       "系統代理",         "System Proxy");
    add("settings.enable_tun_mode",         "TUN 模式",       "TUN 模式",         "TUN Mode");
    add("settings.enable_service_mode",     "服务模式",       "服務模式",         "Service Mode");
    add("settings.enable_auto_check_update","自动检查更新",   "自動檢查更新",     "Auto Check Update");
    add("settings.clash_core",              "Clash 核心",     "Clash 核心",       "Clash Core");
    add("settings.verge_mixed_port",        "混合端口",       "混合連接埠",       "Mixed Port");
    add("settings.proxy_guard_interval",    "代理守卫间隔",   "代理守衛間隔",     "Proxy Guard Interval");
    add("settings.system_proxy_bypass",     "代理绕过",       "代理繞過",         "Proxy Bypass");
    add("settings.always_on_top",           "窗口置顶",       "視窗置頂",         "Always on Top");
    add("settings.lighten_animation",       "减弱动画效果",   "減弱動畫效果",     "Lighten Animation");

    // ---- 提示信息 ----
    add("msg.update_available",      "发现新版本",           "發現新版本",           "New version available");
    add("msg.no_update",             "当前已是最新版本",     "目前已是最新版本",     "You're using the latest version");
    add("msg.update_check_failed",   "检查更新失败",         "檢查更新失敗",         "Failed to check for updates");
    add("msg.save_success",          "保存成功",             "儲存成功",             "Saved successfully");
    add("msg.save_failed",           "保存失败",             "儲存失敗",             "Save failed");
    add("msg.delete_confirm",        "确认删除？",           "確認刪除？",           "Confirm delete?");
    add("msg.instance_running",      "已有实例运行",         "已有實例執行",         "Another instance is running");
    add("msg.profile_imported",      "订阅导入成功",         "訂閱匯入成功",         "Profile imported successfully");
    add("msg.profile_import_failed", "订阅导入失败",         "訂閱匯入失敗",         "Profile import failed");
    add("msg.core_started",          "核心已启动",           "核心已啟動",           "Core started");
    add("msg.core_stopped",          "核心已停止",           "核心已停止",           "Core stopped");
    add("msg.core_start_failed",     "核心启动失败",         "核心啟動失敗",         "Core start failed");
    add("msg.copy_success",          "已复制到剪贴板",       "已複製到剪貼簿",       "Copied to clipboard");
    add("msg.profile_count",         "共 %s 个订阅",         "共 %s 個訂閱",         "%s profiles total");

    // ---- 托盘菜单 ----
    add("tray.open_window",    "打开窗口",   "開啟視窗",   "Open Window");
    add("tray.restart_core",   "重启核心",   "重啟核心",   "Restart Core");
    add("tray.quit",           "退出",       "結束",       "Quit");
    add("tray.mode_rule",      "规则模式",   "規則模式",   "Rule Mode");
    add("tray.mode_global",    "全局模式",   "全域模式",   "Global Mode");
    add("tray.mode_direct",    "直连模式",   "直連模式",   "Direct Mode");
    add("tray.system_proxy",   "系统代理",   "系統代理",   "System Proxy");
    add("tray.tun_mode",       "TUN 模式",   "TUN 模式",   "TUN Mode");

    // ---- 应用通用 ----
    add("app.title",   "ClashYuka", "ClashYuka", "ClashYuka");
    add("app.version", "版本 %s",     "版本 %s",     "Version %s");
}

void I18n::SetLanguage(const std::string& lang) {
    current_lang_ = lang;
}

wxString I18n::Tr(const std::string& key) const {
    auto it = table_.find(key);
    if (it == table_.end()) {
        return wxString::FromUTF8(key);
    }
    // 优先当前语言
    auto lit = it->second.find(current_lang_);
    if (lit != it->second.end()) {
        return lit->second;
    }
    // 回退到简体中文
    lit = it->second.find("zh-cn");
    if (lit != it->second.end()) {
        return lit->second;
    }
    // 回退到英文
    lit = it->second.find("en");
    if (lit != it->second.end()) {
        return lit->second;
    }
    return wxString::FromUTF8(key);
}

wxString I18n::Tr(const std::string& key, const wxString& arg1) const {
    return wxString::Format(Tr(key), arg1);
}

} // namespace clash_yuka::core
