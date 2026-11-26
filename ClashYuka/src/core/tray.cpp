// ClashYuka 系统托盘图标实现
#include "core/tray.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/app.h>
#include <wx/artprov.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/taskbar.h>
#include <wx/toplevel.h>

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/app_context.h"
#include "core/sysopt.h"
#include "core/utils/dirs.h"

namespace clash_yuka::core {

namespace {

// wxTaskBarIcon 子类：重写 CreatePopupMenu 以返回由 TrayIcon 构建的菜单。
// wxWidgets 框架会在用户右键单击托盘图标时调用此方法，并在菜单关闭后删除返回的菜单。
class TrayIconImpl : public wxTaskBarIcon {
public:
    explicit TrayIconImpl(TrayIcon* owner) : owner_(owner) {}

    wxMenu* CreatePopupMenu() override {
        return owner_->BuildPopupMenu();
    }

private:
    TrayIcon* owner_;
};

// 根据 Selector 类型筛选代理组
std::vector<clash::ProxyGroup> FilterSelectorGroups(
    const std::vector<clash::ProxyGroup>& groups) {
    std::vector<clash::ProxyGroup> result;
    for (const auto& g : groups) {
        if (g.type == "Selector") {
            result.push_back(g);
        }
    }
    return result;
}

} // namespace

TrayIcon& TrayIcon::Get() {
    static TrayIcon instance;
    return instance;
}

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Destroy();
}

void TrayIcon::Create() {
    if (tray_ != nullptr) return;

    tray_ = new TrayIconImpl(this);

    // 设置托盘图标（使用 wxArtProvider 默认图标，后续阶段替换为自定义图标）
    wxIcon icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
    if (icon.IsOk()) {
        tray_->SetIcon(icon, wxString::FromUTF8("ClashYuka"));
    } else {
        // 回退：使用空图标但仍设置 tooltip
        tray_->SetIcon(icon, wxString::FromUTF8("ClashYuka"));
    }

    // 绑定左键单击事件：显示/隐藏主窗口
    tray_->Bind(wxEVT_TASKBAR_LEFT_UP, &TrayIcon::OnLeftClick, this);

    // 绑定菜单项事件（静态 ID 范围）
    tray_->Bind(wxEVT_MENU, &TrayIcon::OnMenuItem, this, ID_SHOW_WINDOW, ID_QUIT);
    // 绑定代理菜单项事件（动态 ID 范围）
    tray_->Bind(wxEVT_MENU, &TrayIcon::OnMenuItem, this, ID_PROXY_BASE, ID_PROXY_MAX);

    // 初始获取代理数据
    UpdateMenu();
}

void TrayIcon::Destroy() {
    if (tray_ == nullptr) return;
    tray_->RemoveIcon();
    delete tray_;
    tray_ = nullptr;
}

void TrayIcon::SetTooltip(const wxString& text) {
    if (tray_ == nullptr) return;
    // SetIcon 同时更新图标与 tooltip，此处需保留原图标
    wxIcon icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
    tray_->SetIcon(icon, text);
}

void TrayIcon::UpdateMenu() {
    FetchProxyData();
}

void TrayIcon::FetchProxyData() {
    // 内核未运行时跳过，避免无意义的 API 调用
    if (!AppContext::Get().IsCoreRunning()) return;
    // 在 detached 线程中获取代理组与当前模式，避免阻塞主线程
    std::thread([]() {
        auto& ctx = AppContext::Get();
        if (!ctx.IsCoreRunning()) return;

        // 获取代理组
        auto groups = ctx.api().GetProxyGroups();

        // 获取当前配置（解析 mode 字段）
        std::string mode;
        std::string config_str = ctx.api().GetConfig();
        if (!config_str.empty()) {
            try {
                auto j = nlohmann::json::parse(config_str);
                if (j.contains("mode") && j["mode"].is_string()) {
                    mode = j["mode"].get<std::string>();
                }
            } catch (const nlohmann::json::exception& e) {
                std::cerr << "[Tray] Failed to parse config: " << e.what() << std::endl;
            }
        }

        // 通过 shared_ptr 避免大对象拷贝，marshal 回主线程更新缓存
        auto groups_ptr = std::make_shared<std::vector<clash::ProxyGroup>>(std::move(groups));
        auto mode_ptr = std::make_shared<std::string>(std::move(mode));

        wxTheApp->CallAfter([groups_ptr, mode_ptr]() {
            auto& tray = TrayIcon::Get();
            tray.proxy_groups_ = std::move(*groups_ptr);
            tray.current_mode_ = std::move(*mode_ptr);
        });
    }).detach();
}

wxMenu* TrayIcon::BuildPopupMenu() {
    auto* menu = new wxMenu();

    // 清空上次的代理项映射
    proxy_item_map_.clear();

    // ---- 打开窗口 ----
    menu->Append(ID_SHOW_WINDOW, wxString::FromUTF8("打开窗口"));
    menu->AppendSeparator();

    // ---- 代理模式（单选）----
    menu->AppendRadioItem(ID_MODE_RULE, wxString::FromUTF8("规则模式"));
    menu->AppendRadioItem(ID_MODE_GLOBAL, wxString::FromUTF8("全局模式"));
    menu->AppendRadioItem(ID_MODE_DIRECT, wxString::FromUTF8("直连模式"));
    // 勾选当前模式
    if (current_mode_ == "rule") {
        menu->Check(ID_MODE_RULE, true);
    } else if (current_mode_ == "global") {
        menu->Check(ID_MODE_GLOBAL, true);
    } else if (current_mode_ == "direct") {
        menu->Check(ID_MODE_DIRECT, true);
    }

    // ---- 系统代理（开关）----
    menu->AppendCheckItem(ID_SYS_PROXY, wxString::FromUTF8("系统代理"));
    bool sys_proxy_on = false;
    if (AppContext::Get().IsInitialized()) {
        sys_proxy_on = AppContext::Get().verge().enable_system_proxy.value_or(false);
    }
    menu->Check(ID_SYS_PROXY, sys_proxy_on);

    // ---- TUN 模式（开关）----
    menu->AppendCheckItem(ID_TUN_MODE, wxString::FromUTF8("TUN 模式"));
    bool tun_on = false;
    if (AppContext::Get().IsInitialized()) {
        tun_on = AppContext::Get().verge().enable_tun_mode.value_or(false);
    }
    menu->Check(ID_TUN_MODE, tun_on);

    // ---- 代理列表（子菜单）----
    // 根据配置决定显示方式：hidden / normal / submenu
    std::string selector_mode = "submenu";
    if (AppContext::Get().IsInitialized()) {
        selector_mode = AppContext::Get().verge().clash_tray_selector.value_or("submenu");
    }

    if (selector_mode != "hidden" && !proxy_groups_.empty()) {
        auto selector_groups = FilterSelectorGroups(proxy_groups_);
        if (!selector_groups.empty()) {
            menu->AppendSeparator();

            int next_id = ID_PROXY_BASE;

            // 始终使用子菜单模式：所有代理组收拢到"代理切换"子菜单下
            // 避免节点太多导致一级菜单过长
            auto* proxy_submenu = new wxMenu();

            if (selector_groups.size() == 1) {
                // 只有一个 Selector 组：直接在子菜单下列出节点
                const auto& group = selector_groups[0];
                for (const auto& node : group.all) {
                    if (next_id > ID_PROXY_MAX) break;
                    proxy_submenu->AppendRadioItem(next_id,
                        wxString::FromUTF8(node.c_str()));
                    if (node == group.now) {
                        proxy_submenu->Check(next_id, true);
                    }
                    proxy_item_map_[next_id] = {group.name, node};
                    ++next_id;
                }
            } else {
                // 多个 Selector 组：每个组一个嵌套子菜单
                for (const auto& group : selector_groups) {
                    if (next_id > ID_PROXY_MAX) break;

                    auto* group_menu = new wxMenu();
                    for (const auto& node : group.all) {
                        if (next_id > ID_PROXY_MAX) break;
                        group_menu->AppendRadioItem(next_id,
                            wxString::FromUTF8(node.c_str()));
                        if (node == group.now) {
                            group_menu->Check(next_id, true);
                        }
                        proxy_item_map_[next_id] = {group.name, node};
                        ++next_id;
                    }
                    proxy_submenu->AppendSubMenu(group_menu,
                        wxString::FromUTF8(group.name.c_str()));
                }
            }

            menu->AppendSubMenu(proxy_submenu,
                wxString::FromUTF8("代理切换"));
        }
    }

    // ---- 重启核心 / 退出 ----
    menu->AppendSeparator();
    menu->Append(ID_RESTART_CORE, wxString::FromUTF8("重启核心"));
    menu->Append(ID_QUIT, wxString::FromUTF8("退出"));

    return menu;
}

void TrayIcon::OnLeftClick(wxTaskBarIconEvent& event) {
    // 左键单击：显示/隐藏主窗口
    wxTheApp->CallAfter([]() {
        auto* top = wxTheApp->GetTopWindow();
        if (!top) return;
        auto* tlw = static_cast<wxTopLevelWindow*>(top);
        if (top->IsShown() && !tlw->IsIconized()) {
            top->Hide();
        } else {
            top->Show(true);
            if (tlw->IsIconized()) {
                tlw->Iconize(false);
            }
            top->Raise();
            top->SetFocus();
        }
    });
    event.Skip();
}

void TrayIcon::OnMenuItem(wxCommandEvent& event) {
    int id = event.GetId();

    switch (id) {
    case ID_SHOW_WINDOW: {
        // 显示并激活主窗口
        wxTheApp->CallAfter([]() {
            auto* top = wxTheApp->GetTopWindow();
            if (top) {
                auto* tlw = static_cast<wxTopLevelWindow*>(top);
                top->Show(true);
                if (tlw->IsIconized()) tlw->Iconize(false);
                top->Raise();
                top->SetFocus();
            }
        });
        break;
    }

    case ID_MODE_RULE:
    case ID_MODE_GLOBAL:
    case ID_MODE_DIRECT: {
        // 切换 mihomo 模式
        std::string mode;
        if (id == ID_MODE_RULE) mode = "rule";
        else if (id == ID_MODE_GLOBAL) mode = "global";
        else mode = "direct";

        // 立即更新缓存（响应性）
        current_mode_ = mode;

        // 在 detached 线程中调用 API
        std::thread([mode]() {
            auto& ctx = AppContext::Get();
            nlohmann::json patch = {{"mode", mode}};
            ctx.api().PatchConfig(patch);
        }).detach();
        break;
    }

    case ID_SYS_PROXY: {
        // 切换系统代理
        auto& ctx = AppContext::Get();
        auto& verge = ctx.verge();
        bool new_state = !verge.enable_system_proxy.value_or(false);
        verge.enable_system_proxy = new_state;

        if (new_state) {
            uint16_t port = ctx.GetMixedPort();
            std::string bypass = verge.system_proxy_bypass.value_or("");
            SysProxy::Get().Enable(port, bypass);
            // 若启用代理守护，启动守护线程
            if (verge.enable_proxy_guard.value_or(false)) {
                uint32_t interval =
                    static_cast<uint32_t>(verge.proxy_guard_interval.value_or(10));
                SysProxy::Get().StartGuard(port, bypass, interval);
            }
        } else {
            SysProxy::Get().StopGuard();
            SysProxy::Get().Disable();
        }

        // 持久化保存配置
        verge.Save(utils::verge_config_path());
        break;
    }

    case ID_TUN_MODE: {
        // 切换 TUN 模式：写入 verge 后通过 ApplyConfig 重建配置并重启内核。
        // TUN 创建虚拟网卡需要管理员权限（服务模式下由服务进程提供）。
        auto& ctx = AppContext::Get();
        auto& verge = ctx.verge();
        bool new_state = !verge.enable_tun_mode.value_or(false);
        if (new_state && !verge.enable_service_mode.value_or(false) &&
            !core::IsProcessElevated()) {
            wxMessageBox(
                wxString::FromUTF8(
                    "TUN 模式需要管理员权限。\n"
                    "请以管理员身份运行 ClashYuka，或改用服务模式。"),
                wxString::FromUTF8("TUN 模式"), wxOK | wxICON_WARNING);
            break;
        }
        verge.enable_tun_mode = new_state;
        verge.Save(utils::verge_config_path());
        std::thread([]() {
            std::string err = AppContext::Get().ApplyConfig();
            if (!err.empty()) {
                std::cerr << "[Tray] 应用 TUN 配置失败: " << err << std::endl;
            }
        }).detach();
        break;
    }

    case ID_RESTART_CORE: {
        // 在 detached 线程中重启核心（通过 AppContext::RestartCore 重建 config.yaml）
        std::thread([]() {
            std::string err = AppContext::Get().RestartCore();
            if (!err.empty()) {
                std::cerr << "[Tray] 重启内核失败: " << err << std::endl;
            }
        }).detach();
        break;
    }

    case ID_QUIT: {
        wxTheApp->CallAfter([]() {
            wxTheApp->Exit();
        });
        break;
    }

    default:
        // 代理项：ID_PROXY_BASE..ID_PROXY_MAX
        if (id >= ID_PROXY_BASE && id <= ID_PROXY_MAX) {
            auto it = proxy_item_map_.find(id);
            if (it != proxy_item_map_.end()) {
                const auto& [group_name, node_name] = it->second;
                // 在 detached 线程中调用 API 切换代理
                std::string group = group_name;
                std::string node = node_name;
                std::thread([group, node]() {
                    auto result = AppContext::Get().api().SelectProxy(group, node);
                    if (!result.ok) {
                        std::cerr << "[Tray] 切换代理失败 (" << group << " -> " << node
                                  << "): " << result.error << std::endl;
                    }
                }).detach();
            }
        }
        break;
    }

    event.Skip();
}

} // namespace clash_yuka::core
