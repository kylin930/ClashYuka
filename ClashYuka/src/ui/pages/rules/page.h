// ClashYuka Rules 页面（Phase 6 Task 13）
//
// 规则列表页面：
//   - 虚拟化 wxListCtrl 展示规则列表（Type/Payload/Proxy）
//   - 数据来源：api.GetRules() 一次性拉取（构造时 + 刷新按钮）
//   - 左侧侧栏按 proxy 筛选
//   - 顶部搜索框过滤 type/payload/proxy
//
// 线程模型：detached 线程调 api.GetRules -> wxTheApp->CallAfter -> 主线程更新；
// alive_ 标志位避免页面析构后访问悬空指针（与 Dashboard 一致）。
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/font.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/clash/types.h"

namespace clash_yuka::ui {

class RulesPage;

// 虚拟化规则列表控件：从 RulesPage 取行数据。
// wxListCtrl LC_VIRTUAL 模式仅请求可见行，可承受大量规则。
class RulesListCtrl : public wxListCtrl {
public:
    RulesListCtrl(wxWindow* parent, wxWindowID id, RulesPage* page);

    wxString OnGetItemText(long item, long column) const override;
    wxListItemAttr* OnGetItemAttr(long item) const override;

private:
    RulesPage* page_;
    // 交替行底色属性
    std::unique_ptr<wxListItemAttr> alt_attr_;
};

// Rules 页面容器：组合工具条、筛选侧栏与虚拟列表。
class RulesPage : public wxPanel {
    friend class RulesListCtrl;

public:
    explicit RulesPage(wxWindow* parent);
    ~RulesPage() override;

private:
    // 列定义
    enum Column {
        Col_Type = 0,
        Col_Payload,
        Col_Proxy
    };

    // ---- 事件处理 ----
    void OnSearch(wxCommandEvent& event);
    void OnFilterSelected(wxCommandEvent& event);
    void OnRefresh(wxCommandEvent& event);

    // ---- 数据加载 ----
    void LoadRules();
    void ApplyRules(std::vector<clash::Rule> rules);
    void RebuildProxySidebar();
    void RebuildFilteredView();

    // ---- UI 控件 ----
    wxTextCtrl* search_box_ = nullptr;
    wxListBox* filter_list_ = nullptr;
    RulesListCtrl* list_ctrl_ = nullptr;
    wxButton* refresh_btn_ = nullptr;
    wxStaticText* count_label_ = nullptr;
    wxStaticText* filter_title_ = nullptr;

    // ---- 数据 ----
    std::vector<clash::Rule> all_rules_;
    std::vector<const clash::Rule*> filtered_view_;
    // 侧栏代理列表；索引 0 固定为 "全部"
    std::vector<std::string> proxies_;
    // 上一次侧栏列表的字节签名，用于检测是否需要重建
    std::string proxies_signature_;
    int selected_proxy_idx_ = 0;
    wxString search_text_;

    // 跨线程存活标志
    std::shared_ptr<std::atomic<bool>> alive_;

    wxDECLARE_EVENT_TABLE();
};

} // namespace clash_yuka::ui
