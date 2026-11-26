// ClashYuka Rules 页面实现（Phase 6 Task 13）
//
// 数据流：
//   - detached 线程调 api.GetRules -> wxTheApp->CallAfter -> 主线程 ApplyRules
//   - 所有 UI 访问均发生在主线程；alive_ 标志位避免析构后悬空
#include "ui/pages/rules/page.h"

#include <wx/sizer.h>

#include <algorithm>
#include <set>
#include <thread>
#include <utility>

#include "core/app_context.h"
#include "core/clash/types.h"

namespace clash_yuka::ui {

namespace {

// 控件命令 ID
constexpr int ID_SEARCH = wxID_HIGHEST + 220;
constexpr int ID_FILTER_LIST = wxID_HIGHEST + 221;
constexpr int ID_LIST = wxID_HIGHEST + 222;
constexpr int ID_REFRESH = wxID_HIGHEST + 223;

// 大小写不敏感子串匹配
bool ContainsCI(const wxString& hay, const wxString& needle) {
    if (needle.empty()) return true;
    return hay.Lower().Contains(needle.Lower());
}

} // namespace

wxBEGIN_EVENT_TABLE(RulesPage, wxPanel)
    EVT_TEXT(ID_SEARCH, RulesPage::OnSearch)
    EVT_LISTBOX(ID_FILTER_LIST, RulesPage::OnFilterSelected)
    EVT_BUTTON(ID_REFRESH, RulesPage::OnRefresh)
wxEND_EVENT_TABLE()

// ============================================================
// RulesListCtrl
// ============================================================

RulesListCtrl::RulesListCtrl(wxWindow* parent, wxWindowID id, RulesPage* page)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL |
                 wxLC_HRULES | wxLC_VRULES),
      page_(page) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

    alt_attr_ = std::make_unique<wxListItemAttr>();
    alt_attr_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

    InsertColumn(RulesPage::Col_Type,    wxString::FromUTF8("类型"),    wxLIST_FORMAT_LEFT, 140);
    InsertColumn(RulesPage::Col_Payload, wxString::FromUTF8("Payload"), wxLIST_FORMAT_LEFT, 360);
    InsertColumn(RulesPage::Col_Proxy,   wxString::FromUTF8("代理"),    wxLIST_FORMAT_LEFT, 180);
    SetItemCount(0);
}

wxString RulesListCtrl::OnGetItemText(long item, long column) const {
    if (!page_ || item < 0 || item >= (long)page_->filtered_view_.size())
        return wxEmptyString;
    const clash::Rule& r = *page_->filtered_view_[item];
    switch (column) {
        case RulesPage::Col_Type:
            return wxString::FromUTF8(r.type);
        case RulesPage::Col_Payload:
            return wxString::FromUTF8(r.payload);
        case RulesPage::Col_Proxy:
            return wxString::FromUTF8(r.proxy);
        default:
            return wxEmptyString;
    }
}

wxListItemAttr* RulesListCtrl::OnGetItemAttr(long item) const {
    // 交替行底色，提升大量行时的可读性
    if (item % 2 == 1) return alt_attr_.get();
    return nullptr;
}

// ============================================================
// RulesPage
// ============================================================

RulesPage::RulesPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ---- 工具条 ----
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    search_box_ = new wxTextCtrl(this, ID_SEARCH, wxEmptyString,
                                 wxDefaultPosition, wxSize(280, -1));
    search_box_->SetHint(wxString::FromUTF8("搜索 类型 / Payload / 代理"));
    toolbar->Add(search_box_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

    count_label_ = new wxStaticText(this, wxID_ANY, "0 / 0");
    count_label_->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    count_label_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    toolbar->Add(count_label_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);

    toolbar->AddStretchSpacer(1);

    refresh_btn_ = new wxButton(this, ID_REFRESH, wxString::FromUTF8("刷新"));
    toolbar->Add(refresh_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    outer->Add(toolbar, 0, wxEXPAND | wxTOP | wxBOTTOM, 6);

    // ---- 主体：侧栏 + 列表 ----
    auto* body = new wxBoxSizer(wxHORIZONTAL);

    // 侧栏：按代理筛选
    auto* side = new wxPanel(this);
    side->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
    auto* sideSizer = new wxBoxSizer(wxVERTICAL);
    filter_title_ = new wxStaticText(side, wxID_ANY,
                                      wxString::FromUTF8("按代理筛选"));
    filter_title_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    filter_title_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sideSizer->Add(filter_title_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 10);

    filter_list_ = new wxListBox(side, ID_FILTER_LIST,
                                 wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                 wxLB_SINGLE | wxBORDER_NONE);
    filter_list_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
    filter_list_->Append(wxString::FromUTF8("全部"));
    filter_list_->SetSelection(0);
    proxies_.push_back("全部");
    sideSizer->Add(filter_list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    side->SetSizer(sideSizer);
    side->SetMinSize(wxSize(180, -1));
    body->Add(side, 0, wxEXPAND);

    // 虚拟列表
    list_ctrl_ = new RulesListCtrl(this, ID_LIST, this);
    body->Add(list_ctrl_, 1, wxEXPAND);

    outer->Add(body, 1, wxEXPAND);
    SetSizer(outer);
    Layout();

    // 构造后立即拉取一次规则
    LoadRules();
}

RulesPage::~RulesPage() {
    // 标记 alive_=false，使已投递但未执行的 CallAfter 跳过 UI 访问
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
}

void RulesPage::OnSearch(wxCommandEvent&) {
    search_text_ = search_box_->GetValue();
    RebuildFilteredView();
}

void RulesPage::OnFilterSelected(wxCommandEvent& event) {
    selected_proxy_idx_ = event.GetInt();
    RebuildFilteredView();
}

void RulesPage::OnRefresh(wxCommandEvent&) {
    LoadRules();
}

void RulesPage::LoadRules() {
    if (!clash_yuka::AppContext::Get().IsInitialized()) return;
    if (!clash_yuka::AppContext::Get().IsCoreRunning()) return;
    auto alive = alive_;
    count_label_->SetLabel(wxString::FromUTF8("加载中..."));
    // detached 线程执行同步 HTTP 调用，避免阻塞 UI 主线程。
    std::thread([this, alive]() {
        if (!alive->load(std::memory_order_acquire)) return;
        auto rules = clash_yuka::AppContext::Get().api().GetRules();
        wxTheApp->CallAfter([this, alive, rules = std::move(rules)]() {
            if (!alive->load(std::memory_order_acquire)) return;
            ApplyRules(std::move(rules));
        });
    }).detach();
}

void RulesPage::ApplyRules(std::vector<clash::Rule> rules) {
    all_rules_ = std::move(rules);
    RebuildProxySidebar();
    RebuildFilteredView();
}

void RulesPage::RebuildProxySidebar() {
    // 收集所有唯一 proxy
    std::set<std::string> unique;
    for (const auto& r : all_rules_) {
        if (!r.proxy.empty()) unique.insert(r.proxy);
    }

    // 计算签名；无变化则跳过重建
    std::string sig;
    for (const auto& p : unique) sig += p + "|";
    if (sig == proxies_signature_) return;
    proxies_signature_ = sig;

    // 记住当前选中，重建后尽量恢复
    wxString prev_sel;
    if (selected_proxy_idx_ >= 0 && selected_proxy_idx_ < (int)proxies_.size()) {
        prev_sel = wxString::FromUTF8(proxies_[selected_proxy_idx_]);
    }

    proxies_.clear();
    proxies_.push_back("全部");
    for (const auto& p : unique) proxies_.push_back(p);

    filter_list_->Clear();
    for (const auto& p : proxies_) filter_list_->Append(wxString::FromUTF8(p));

    int new_sel = 0;
    if (!prev_sel.empty()) {
        for (size_t i = 0; i < proxies_.size(); ++i) {
            if (wxString::FromUTF8(proxies_[i]) == prev_sel) {
                new_sel = (int)i;
                break;
            }
        }
    }
    selected_proxy_idx_ = new_sel;
    filter_list_->SetSelection(new_sel);
}

void RulesPage::RebuildFilteredView() {
    // 当前选中的 proxy（"全部" 时不筛选）
    const std::string* proxy_filter = nullptr;
    if (selected_proxy_idx_ > 0 && selected_proxy_idx_ < (int)proxies_.size()) {
        proxy_filter = &proxies_[selected_proxy_idx_];
    }

    filtered_view_.clear();
    filtered_view_.reserve(all_rules_.size());
    for (const auto& r : all_rules_) {
        // proxy 筛选
        if (proxy_filter && r.proxy != *proxy_filter) continue;
        // 搜索筛选（type / payload / proxy）
        if (!search_text_.empty()) {
            wxString hay = wxString::FromUTF8(r.type) + " " +
                           wxString::FromUTF8(r.payload) + " " +
                           wxString::FromUTF8(r.proxy);
            if (!ContainsCI(hay, search_text_)) continue;
        }
        filtered_view_.push_back(&r);
    }

    list_ctrl_->SetItemCount(static_cast<long>(filtered_view_.size()));
    list_ctrl_->Refresh();

    count_label_->SetLabel(wxString::Format("%d / %d",
        (int)filtered_view_.size(), (int)all_rules_.size()));
}

} // namespace clash_yuka::ui
