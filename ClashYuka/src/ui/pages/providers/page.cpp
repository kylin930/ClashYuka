// ClashYuka Providers 页面实现（Phase 6 Task 15）
#include "ui/pages/providers/page.h"

#include <wx/sizer.h>

#include <thread>
#include <utility>

#include "core/app_context.h"

namespace clash_yuka::ui {

namespace {

wxStaticText* MakeLabel(wxWindow* parent, const wxString& text) {
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return t;
}

} // namespace

// ============================================================
// ProvidersPage
// ============================================================

ProvidersPage::ProvidersPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ---- 顶部：Choice + 刷新按钮 ----
    auto* top_row = new wxBoxSizer(wxHORIZONTAL);

    kind_choice_ = new wxChoice(this, wxID_ANY);
    kind_choice_->Append(wxString::FromUTF8("代理提供者"));
    kind_choice_->Append(wxString::FromUTF8("规则提供者"));
    kind_choice_->SetSelection(0);
    kind_choice_->Bind(wxEVT_CHOICE, &ProvidersPage::OnTabChanged, this);
    top_row->Add(kind_choice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    refresh_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("刷新"));
    refresh_btn_->Bind(wxEVT_BUTTON, &ProvidersPage::OnRefresh, this);
    top_row->Add(refresh_btn_, 0, wxALIGN_CENTER_VERTICAL);

    outer->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    // ---- 内容区：左侧列表 + 右侧详情 ----
    auto* body = new wxBoxSizer(wxHORIZONTAL);

    // 左侧：可滚动的 provider 列表
    list_scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize, wxVSCROLL);
    list_scroll_->SetScrollRate(5, 5);
    list_scroll_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    list_scroll_->SetMinSize(wxSize(280, -1));
    auto* list_sizer = new wxBoxSizer(wxVERTICAL);
    list_scroll_->SetSizer(list_sizer);
    body->Add(list_scroll_, 0, wxEXPAND | wxRIGHT, 8);

    // 右侧：详情 Panel
    detail_card_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxSIMPLE_BORDER);
    detail_card_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    {
        auto* card_sizer = new wxBoxSizer(wxVERTICAL);

        // 标题
        auto* title = new wxStaticText(detail_card_, wxID_ANY,
            wxString::FromUTF8("提供者详情"));
        title->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        card_sizer->Add(title, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 16);

        // 名称（大字）
        detail_name_ = new wxStaticText(detail_card_, wxID_ANY,
            wxString::FromUTF8("--"));
        detail_name_->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        detail_name_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
        card_sizer->Add(detail_name_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 12);

        // label-value 网格（2 列，行数自动；4 参数版会限制 2x2=4 格导致 assert）
        auto* grid = new wxFlexGridSizer(2, 8, 24);

        grid->Add(MakeLabel(detail_card_, wxString::FromUTF8("类型")), 0, wxALIGN_CENTER_VERTICAL);
        detail_type_value_ = new wxStaticText(detail_card_, wxID_ANY, "--");
        detail_type_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        detail_type_value_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        grid->Add(detail_type_value_, 0, wxALIGN_CENTER_VERTICAL);

        grid->Add(MakeLabel(detail_card_, wxString::FromUTF8("Vehicle")), 0, wxALIGN_CENTER_VERTICAL);
        detail_vehicle_value_ = new wxStaticText(detail_card_, wxID_ANY, "--");
        detail_vehicle_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        detail_vehicle_value_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        grid->Add(detail_vehicle_value_, 0, wxALIGN_CENTER_VERTICAL);

        grid->Add(MakeLabel(detail_card_, wxString::FromUTF8("更新时间")), 0, wxALIGN_CENTER_VERTICAL);
        detail_updated_value_ = new wxStaticText(detail_card_, wxID_ANY, "--");
        detail_updated_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        detail_updated_value_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        grid->Add(detail_updated_value_, 0, wxALIGN_CENTER_VERTICAL);

        grid->Add(MakeLabel(detail_card_, wxString::FromUTF8("订阅信息")), 0, wxALIGN_CENTER_VERTICAL);
        detail_subinfo_value_ = new wxStaticText(detail_card_, wxID_ANY, "--");
        detail_subinfo_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        detail_subinfo_value_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        grid->Add(detail_subinfo_value_, 0, wxALIGN_CENTER_VERTICAL);

        card_sizer->Add(grid, 0, wxLEFT | wxTOP, 16);

        // 按钮行
        auto* btn_row = new wxBoxSizer(wxHORIZONTAL);

        update_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("更新"));
        update_btn_->Bind(wxEVT_BUTTON, &ProvidersPage::OnUpdate, this);
        update_btn_->Disable();
        btn_row->Add(update_btn_, 0, wxRIGHT, 8);

        healthcheck_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("健康检查"));
        healthcheck_btn_->Bind(wxEVT_BUTTON, &ProvidersPage::OnHealthCheck, this);
        healthcheck_btn_->Disable();
        btn_row->Add(healthcheck_btn_, 0, wxRIGHT, 8);

        card_sizer->Add(btn_row, 0, wxLEFT | wxTOP, 16);

        // 状态文本
        status_text_ = new wxStaticText(detail_card_, wxID_ANY,
            wxString::FromUTF8("就绪"));
        status_text_->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        status_text_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        card_sizer->Add(status_text_, 0, wxALIGN_LEFT | wxLEFT | wxTOP | wxBOTTOM, 16);

        detail_card_->SetSizer(card_sizer);
        detail_card_->SetMinSize(wxSize(320, 220));
    }
    body->Add(detail_card_, 1, wxEXPAND);

    outer->Add(body, 1, wxEXPAND | wxALL, 8);
    SetSizer(outer);
    Layout();

    if (clash_yuka::AppContext::Get().IsInitialized()) {
        LoadProviders(ProviderKind::Proxy);
    }
}

ProvidersPage::~ProvidersPage() {
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
}

// ---- 事件处理 ----

void ProvidersPage::OnTabChanged(wxCommandEvent& event) {
    int sel = event.GetInt();
    if (sel == 0) {
        LoadProviders(ProviderKind::Proxy);
    } else if (sel == 1) {
        LoadProviders(ProviderKind::Rule);
    }
}

void ProvidersPage::OnRefresh(wxCommandEvent&) {
    LoadProviders(current_kind_);
}

void ProvidersPage::OnListSelected(wxCommandEvent& event) {
    int index = event.GetInt();
    UpdateDetail(index);
}

void ProvidersPage::OnUpdate(wxCommandEvent&) {
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(providers_.size())) {
        return;
    }
    const std::string name = providers_[selected_index_].name;
    const std::string api_type =
        (current_kind_ == ProviderKind::Proxy) ? "proxies" : "rules";

    SetStatus(wxString::FromUTF8("更新中..."));
    update_btn_->Disable();
    healthcheck_btn_->Disable();

    auto alive = alive_;
    const ProviderKind kind = current_kind_;
    std::thread([this, alive, name, api_type, kind]() {
        if (!alive->load(std::memory_order_acquire)) return;
        bool ok = clash_yuka::AppContext::Get().api().UpdateProvider(api_type, name);
        wxTheApp->CallAfter([this, alive, ok, kind]() {
            if (!alive->load(std::memory_order_acquire)) return;
            SetStatus(ok ? wxString::FromUTF8("更新成功") : wxString::FromUTF8("更新失败"));
            if (ok && current_kind_ == kind) {
                LoadProviders(kind);  // 重新加载列表
            }
        });
    }).detach();
}

void ProvidersPage::OnHealthCheck(wxCommandEvent&) {
    if (current_kind_ != ProviderKind::Proxy) return;
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(providers_.size())) {
        return;
    }
    const std::string name = providers_[selected_index_].name;

    SetStatus(wxString::FromUTF8("健康检查中..."));
    healthcheck_btn_->Disable();

    auto alive = alive_;
    std::thread([this, alive, name]() {
        if (!alive->load(std::memory_order_acquire)) return;
        bool ok = clash_yuka::AppContext::Get().api().HealthCheckProvider(name);
        wxTheApp->CallAfter([this, alive, ok]() {
            if (!alive->load(std::memory_order_acquire)) return;
            SetStatus(ok ? wxString::FromUTF8("健康检查已触发") : wxString::FromUTF8("健康检查失败"));
        });
    }).detach();
}

// ---- 数据加载 ----

void ProvidersPage::LoadProviders(ProviderKind kind) {
    current_kind_ = kind;
    if (!clash_yuka::AppContext::Get().IsCoreRunning()) {
        SetStatus(wxString::FromUTF8("内核未运行"));
        return;
    }
    SetStatus(wxString::FromUTF8("加载中..."));

    auto alive = alive_;
    std::thread([this, alive, kind]() {
        if (!alive->load(std::memory_order_acquire)) return;
        auto& api = clash_yuka::AppContext::Get().api();
        std::vector<clash::Provider> result;
        if (kind == ProviderKind::Proxy) {
            result = api.GetProxyProviders();
        } else {
            result = api.GetRuleProviders();
        }

        wxTheApp->CallAfter([this, alive, kind, result = std::move(result)]() {
            if (!alive->load(std::memory_order_acquire)) return;
            // 若加载期间用户已切换 Tab，丢弃过期结果
            if (current_kind_ != kind) return;

            // 记录当前选中的 name，加载后尝试恢复选中
            std::string prev_name;
            if (selected_index_ >= 0 &&
                selected_index_ < static_cast<int>(providers_.size())) {
                prev_name = providers_[selected_index_].name;
            }

            providers_ = std::move(result);
            RebuildList();

            // 尝试按 name 恢复选中；否则选中第一个
            int new_index = -1;
            if (!prev_name.empty()) {
                for (size_t i = 0; i < providers_.size(); ++i) {
                    if (providers_[i].name == prev_name) {
                        new_index = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (new_index < 0 && !providers_.empty()) {
                new_index = 0;
            }

            if (new_index >= 0) {
                list_->SetSelection(new_index);
            }
            UpdateDetail(new_index);

            SetStatus(wxString::Format(wxString::FromUTF8("已加载 %d 个提供者"),
                                       static_cast<int>(providers_.size())));
        });
    }).detach();
}

void ProvidersPage::RebuildList() {
    auto* sizer = list_scroll_->GetSizer();
    if (list_) {
        sizer->Detach(list_);
        list_->Destroy();
        list_ = nullptr;
    }

    list_ = new wxListBox(list_scroll_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          0, nullptr, wxLB_SINGLE | wxLB_HSCROLL);
    list_->Bind(wxEVT_LISTBOX, &ProvidersPage::OnListSelected, this);

    for (const auto& p : providers_) {
        wxString supporting = wxString::FromUTF8(p.name) + " (" + wxString::FromUTF8(p.vehicleType);
        if (p.updatedAt && !p.updatedAt->empty()) {
            supporting += ", " + wxString::FromUTF8(*p.updatedAt);
        }
        supporting += ")";
        list_->Append(supporting);
    }

    sizer->Add(list_, 1, wxEXPAND);
    list_scroll_->FitInside();
    list_scroll_->Layout();
}

void ProvidersPage::UpdateDetail(int index) {
    selected_index_ = index;

    if (index < 0 || index >= static_cast<int>(providers_.size())) {
        detail_name_->SetLabel("--");
        detail_type_value_->SetLabel("--");
        detail_vehicle_value_->SetLabel("--");
        detail_updated_value_->SetLabel("--");
        detail_subinfo_value_->SetLabel("--");
        update_btn_->Disable();
        healthcheck_btn_->Disable();
        return;
    }

    const auto& p = providers_[index];
    detail_name_->SetLabel(wxString::FromUTF8(p.name));
    detail_type_value_->SetLabel(wxString::FromUTF8(p.type));
    detail_vehicle_value_->SetLabel(wxString::FromUTF8(p.vehicleType));
    detail_updated_value_->SetLabel(
        p.updatedAt ? wxString::FromUTF8(*p.updatedAt) : wxString::FromUTF8("N/A"));
    detail_subinfo_value_->SetLabel(
        p.subscriptionInfo
            ? wxString::Format("%d", *p.subscriptionInfo)
            : wxString::FromUTF8("N/A"));

    update_btn_->Enable();
    // 健康检查仅对 proxy provider 可用
    healthcheck_btn_->Enable(current_kind_ == ProviderKind::Proxy);
}

void ProvidersPage::SetStatus(const wxString& text) {
    status_text_->SetLabel(text);
}

} // namespace clash_yuka::ui
