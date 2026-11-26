// ClashYuka 系统托盘图标（Phase 9 Task 20）
//
// 基于 wxTaskBarIcon 实现系统托盘图标与右键菜单。
//
// 右键菜单结构：
//   打开窗口
//   ─────────
//   规则模式 / 全局模式 / 直连模式（单选）
//   系统代理（开关）
//   TUN 模式（开关）
//   ─────────
//   代理列表（子菜单，从 mihomo /proxies 获取 Selector 组）
//   ─────────
//   重启核心
//   退出
//
// 左键单击：显示/隐藏主窗口。
// 菜单项的 API 调用通过 wxTheApp->CallAfter + detached thread 异步执行。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wx/menu.h>
#include <wx/string.h>
#include <wx/taskbar.h>

#include "core/clash/types.h"

namespace clash_yuka::core {

class TrayIcon {
public:
    static TrayIcon& Get();

    // 禁止拷贝/移动
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    // 创建托盘图标（应用启动时调用）
    void Create();
    // 销毁托盘图标
    void Destroy();
    // 更新托盘菜单（异步获取代理列表、模式等，完成后刷新缓存）
    void UpdateMenu();
    // 设置托盘提示文字
    void SetTooltip(const wxString& text);

    // 构建并返回弹出菜单（由 wxTaskBarIcon::CreatePopupMenu 调用，调用方负责删除）
    // public：TrayIconImpl（.cpp 中定义的 wxTaskBarIcon 子类）需调用此方法
    wxMenu* BuildPopupMenu();

private:
    TrayIcon();
    ~TrayIcon();

    // 左键单击：显示/隐藏主窗口
    void OnLeftClick(wxTaskBarIconEvent& event);
    // 菜单项点击处理
    void OnMenuItem(wxCommandEvent& event);

    // 异步获取代理组与当前模式，结果在主线程更新缓存
    void FetchProxyData();

    // 菜单项 ID 常量
    static constexpr int ID_SHOW_WINDOW = 10000;
    static constexpr int ID_MODE_RULE = 10001;
    static constexpr int ID_MODE_GLOBAL = 10002;
    static constexpr int ID_MODE_DIRECT = 10003;
    static constexpr int ID_SYS_PROXY = 10004;
    static constexpr int ID_TUN_MODE = 10005;
    static constexpr int ID_RESTART_CORE = 10006;
    static constexpr int ID_QUIT = 10007;
    // 代理项 ID 从 10100 开始
    static constexpr int ID_PROXY_BASE = 10100;
    // 代理项 ID 上限（防止溢出）
    static constexpr int ID_PROXY_MAX = 10999;

    wxTaskBarIcon* tray_ = nullptr;

    // 缓存的代理组数据（由 FetchProxyData 在主线程更新）
    std::vector<clash::ProxyGroup> proxy_groups_;
    // 缓存的当前模式："rule" / "global" / "direct"
    std::string current_mode_;
    // 代理菜单项 ID -> (组名, 节点名) 映射
    std::unordered_map<int, std::pair<std::string, std::string>> proxy_item_map_;
};

} // namespace clash_yuka::core
