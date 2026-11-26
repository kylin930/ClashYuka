// ClashYuka Providers 页面（Phase 6 Task 15）
//
// 代理/规则提供者管理：
//   - 左侧：proxy/rule provider 列表（TabBar 切换），用列表控件
//     显示 name、vehicleType、updatedAt
//   - 右侧：选中 provider 的详情（name/type/vehicleType/updatedAt/订阅信息）
//     - 更新按钮：api.UpdateProvider("proxies"|"rules", name)
//     - 健康检查按钮（仅 proxy provider）：api.HealthCheckProvider(name)
//
// 数据来源与线程模型：
//   - api.GetProxyProviders() / GetRuleProviders() 在 detached 线程中同步调用，
//     结果通过 CallAfter 回主线程更新 UI
//   - 更新/健康检查同理：detached 线程调用 -> CallAfter 回主线程刷新
//   - alive_ 标志位避免页面析构后访问悬空指针
//
// 布局：
//   ┌──────────────────────────────────────────────┐
//   │ [TabBar: 代理提供者 | 规则提供者]    [刷新]   │
//   ├──────────────┬───────────────────────────────┤
//   │ List (scroll)│ Detail Card                   │
//   │  - provider1 │  name (大字)                  │
//   │  - provider2 │  类型 / Vehicle / 更新时间    │
//   │  ...         │  [更新] [健康检查]             │
//   │              │  状态文本                      │
//   └──────────────┴───────────────────────────────┘
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/listbox.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/clash/types.h"

namespace clash_yuka::ui {

class ProvidersPage : public wxPanel {
public:
    explicit ProvidersPage(wxWindow* parent);
    ~ProvidersPage() override;

private:
    // 提供者类型
    enum class ProviderKind { Proxy, Rule };

    // ---- 事件处理 ----
    void OnTabChanged(wxCommandEvent& event);
    void OnRefresh(wxCommandEvent& event);
    void OnListSelected(wxCommandEvent& event);
    void OnUpdate(wxCommandEvent& event);
    void OnHealthCheck(wxCommandEvent& event);

    // ---- 数据加载（detached 线程 + CallAfter）----
    void LoadProviders(ProviderKind kind);
    void RebuildList();
    void UpdateDetail(int index);
    void SetStatus(const wxString& text);

    // 控件：顶部
    wxChoice* kind_choice_ = nullptr;
    wxButton* refresh_btn_ = nullptr;

    // 控件：左侧列表
    wxScrolledWindow* list_scroll_ = nullptr;
    wxListBox* list_ = nullptr;

    // 控件：右侧详情
    wxPanel* detail_card_ = nullptr;
    wxStaticText* detail_name_ = nullptr;
    wxStaticText* detail_type_value_ = nullptr;
    wxStaticText* detail_vehicle_value_ = nullptr;
    wxStaticText* detail_updated_value_ = nullptr;
    wxStaticText* detail_subinfo_value_ = nullptr;
    wxButton* update_btn_ = nullptr;
    wxButton* healthcheck_btn_ = nullptr;
    wxStaticText* status_text_ = nullptr;

    // 数据
    ProviderKind current_kind_ = ProviderKind::Proxy;
    std::vector<clash::Provider> providers_;
    int selected_index_ = -1;

    // 跨线程存活标志
    std::shared_ptr<std::atomic<bool>> alive_;
};

} // namespace clash_yuka::ui
