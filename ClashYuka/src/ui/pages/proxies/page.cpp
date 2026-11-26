// ClashYuka Proxies 页面实现（Phase 5 Task 11）
#include "ui/pages/proxies/page.h"

#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/app_context.h"
#include "core/clash/types.h"

namespace clash_yuka::ui {

namespace {

// wxString -> std::string（UTF-8）。ToUTF8() 返回的 scoped buffer 在表达式内有效，
// 立即构造 std::string 拷贝字符。
std::string ToUtf8(const wxString& s) {
    const char* p = s.ToUTF8().data();
    return p ? std::string(p) : std::string();
}

// 延迟排序键：未测(-1) 排最后；失败(0) 次后；其余按 ms 升序。
int DelaySortKey(int d) {
    if (d < 0) return INT_MAX;
    if (d == 0) return INT_MAX - 1;
    return d;
}

// 工作线程使用的纯 std::string 数据，避免在后台线程构造 wxString。
struct RawNode {
    std::string name;
    std::string type;
    int delay = -1;
};

struct RawGroup {
    std::string name;
    std::string type;
    std::string now;
    std::vector<RawNode> nodes;
};

// 自动策略组类型判断（用于提示「手动选择会固定节点」）
bool IsAutoGroupType(const wxString& type) {
    return type.IsSameAs("URLTest") || type.IsSameAs("Fallback") ||
           type.IsSameAs("LoadBalance");
}

} // namespace

// ============================================================
// NodeListView
// ============================================================

NodeListView::NodeListView(wxWindow* parent, wxWindowID id)
    : wxScrolled<wxPanel>(parent, id, wxDefaultPosition, wxDefaultSize,
                          wxBORDER_NONE) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    SetScrollRate(0, kRowHeight);
    SetVirtualSize(0, 0);

    Bind(wxEVT_PAINT, &NodeListView::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &NodeListView::OnLeftDown, this);
    Bind(wxEVT_ERASE_BACKGROUND, &NodeListView::OnEraseBackground, this);
    Bind(wxEVT_SIZE, &NodeListView::OnSize, this);
}

void NodeListView::SetNodes(const std::vector<ProxyNodeEntry>& nodes,
                            const wxString& current_name) {
    raw_ = nodes;
    current_name_ = current_name;
    Rebuild();
}

void NodeListView::UpdateDelay(const wxString& name, int delay_ms) {
    // 原地更新（不重排，避免批量测速时列表跳动）。
    bool changed = false;
    for (auto& n : raw_) {
        if (n.name.IsSameAs(name)) { n.delay = delay_ms; changed = true; break; }
    }
    for (auto& n : shown_) {
        if (n.name.IsSameAs(name)) { n.delay = delay_ms; changed = true; }
    }
    if (changed) Refresh();
}

void NodeListView::SetCurrent(const wxString& name) {
    current_name_ = name;
    Refresh();
}

void NodeListView::SetFilter(const wxString& keyword) {
    filter_ = keyword;
    Rebuild();
}

void NodeListView::SetSortMode(ProxySortMode mode) {
    sort_mode_ = mode;
    Rebuild();
}

void NodeListView::Rebuild() {
    shown_.clear();
    const wxString f = filter_.Lower();
    for (const auto& n : raw_) {
        if (!f.empty() && n.name.Lower().Find(f) == wxNOT_FOUND) continue;
        shown_.push_back(n);
    }
    switch (sort_mode_) {
        case ProxySortMode::ByName:
            std::sort(shown_.begin(), shown_.end(),
                      [](const ProxyNodeEntry& a, const ProxyNodeEntry& b) {
                          return a.name.CmpNoCase(b.name) < 0;
                      });
            break;
        case ProxySortMode::ByDelay:
            std::sort(shown_.begin(), shown_.end(),
                      [](const ProxyNodeEntry& a, const ProxyNodeEntry& b) {
                          return DelaySortKey(a.delay) < DelaySortKey(b.delay);
                      });
            break;
        case ProxySortMode::Default:
        default:
            break;
    }
    const int w = GetClientSize().GetWidth();
    SetVirtualSize(w, static_cast<int>(shown_.size()) * kRowHeight);
    Refresh();
}

void NodeListView::DelayColors(int delay, wxString& label,
                               wxColour& bg, wxColour& fg) {
    if (delay < 0) {
        label = "--";
        bg = wxColour(241, 241, 245);
        fg = wxColour(113, 113, 122);
    } else if (delay == 0) {
        label = wxString::FromUTF8("timeout");
        bg = wxColour(254, 226, 226);
        fg = wxColour(185, 28, 28);
    } else if (delay < 100) {
        label = wxString::Format("%dms", delay);
        bg = wxColour(34, 197, 94);
        fg = *wxWHITE;
    } else if (delay < 300) {
        label = wxString::Format("%dms", delay);
        bg = wxColour(234, 179, 8);
        fg = wxColour(133, 77, 14);
    } else {
        label = wxString::Format("%dms", delay);
        bg = wxColour(239, 68, 68);
        fg = *wxWHITE;
    }
}

void NodeListView::DrawChip(wxDC& dc, int x, int y, int w, int h,
                            const wxString& label,
                            const wxColour& bg, const wxColour& fg) {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg));
    dc.DrawRoundedRectangle(x, y, w, h, h / 2);
    dc.SetTextForeground(fg);
    const wxSize ts = dc.GetTextExtent(label);
    dc.DrawText(label, x + (w - ts.GetWidth()) / 2,
                y + (h - ts.GetHeight()) / 2);
}

void NodeListView::OnEraseBackground(wxEraseEvent&) {
    // 不擦除背景，由 OnPaint 全量重绘（避免闪烁）。
}

void NodeListView::OnSize(wxSizeEvent& event) {
    const int w = GetClientSize().GetWidth();
    const int h = static_cast<int>(shown_.size()) * kRowHeight;
    SetVirtualSize(w, h);
    Refresh();
    event.Skip();
}

void NodeListView::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    PrepareDC(dc);

    const wxColour surface = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    const wxColour primary = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    const wxColour primary_container = wxColour(232, 241, 254);
    const wxColour on_primary_container = wxColour(0, 32, 77);
    const wxColour on_surface = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    const wxColour on_surface_variant = wxColour(113, 113, 122);
    const wxColour outline_variant = wxColour(212, 212, 216);

    const wxSize client = GetClientSize();
    const wxPoint tl = CalcUnscrolledPosition(wxPoint(0, 0));

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(surface));
    dc.DrawRectangle(tl.x, tl.y, client.GetWidth(), client.GetHeight());

    if (shown_.empty()) return;

    int first = tl.y / kRowHeight;
    int last = (tl.y + client.GetHeight()) / kRowHeight;
    first = std::max(0, first);
    last = std::min(static_cast<int>(shown_.size()) - 1, last);

    const wxFont name_font(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    const wxFont type_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    const wxFont chip_font(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    const int text_x = 16;
    const int chip_x_right_margin = 12;

    for (int i = first; i <= last; ++i) {
        const auto& n = shown_[i];
        const int y = i * kRowHeight;
        const bool is_current = n.name.IsSameAs(current_name_);

        if (is_current) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(primary_container));
            dc.DrawRectangle(0, y, client.GetWidth(), kRowHeight);
            dc.SetBrush(wxBrush(primary));
            dc.DrawRectangle(0, y, 3, kRowHeight);
        }

        dc.SetFont(name_font);
        dc.SetTextForeground(is_current ? on_primary_container : on_surface);
        const wxSize name_sz = dc.GetTextExtent(n.name);
        dc.DrawText(n.name, text_x,
                    y + (kRowHeight - dc.GetCharHeight()) / 2);

        if (!n.type.empty()) {
            dc.SetFont(type_font);
            dc.SetTextForeground(on_surface_variant);
            dc.DrawText(n.type, text_x + name_sz.GetWidth() + 12,
                        y + (kRowHeight - dc.GetCharHeight()) / 2);
        }

        wxString label;
        wxColour bg, fg;
        DelayColors(n.delay, label, bg, fg);
        const int chip_x = client.GetWidth() - kChipWidth - chip_x_right_margin;
        const int chip_y = y + (kRowHeight - kChipHeight) / 2;
        dc.SetFont(chip_font);
        DrawChip(dc, chip_x, chip_y, kChipWidth, kChipHeight, label, bg, fg);

        dc.SetPen(wxPen(outline_variant));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLine(0, y + kRowHeight - 1,
                    client.GetWidth(), y + kRowHeight - 1);
    }
}

void NodeListView::OnLeftDown(wxMouseEvent& event) {
    const wxPoint pt = event.GetPosition();
    const wxPoint vp = CalcUnscrolledPosition(pt);
    const int row = vp.y / kRowHeight;
    if (row >= 0 && row < static_cast<int>(shown_.size())) {
        if (on_node_clicked) on_node_clicked(shown_[row].name);
    }
    event.Skip();
}

// ============================================================
// ProxiesPage
// ============================================================

ProxiesPage::ProxiesPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)),
      load_gen_(std::make_shared<std::atomic<int>>(0)) {
    const wxColour surface = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    const wxColour on_surface_variant = wxColour(113, 113, 122);
    const wxColour surface_container_low = wxColour(248, 248, 250);
    SetBackgroundColour(surface);

    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* tb = new wxBoxSizer(wxHORIZONTAL);

    auto make_label = [&](const wxString& text) {
        auto* t = new wxStaticText(this, wxID_ANY, text);
        t->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        t->SetForegroundColour(on_surface_variant);
        return t;
    };

    tb->Add(make_label(wxString::FromUTF8("模式")),
            0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    mode_choice_ = new wxChoice(this, wxID_ANY);
    mode_choice_->Append(wxString::FromUTF8("rule"));
    mode_choice_->Append(wxString::FromUTF8("global"));
    mode_choice_->Append(wxString::FromUTF8("direct"));
    mode_choice_->SetSelection(0);
    tb->Add(mode_choice_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

    tb->Add(make_label(wxString::FromUTF8("搜索")),
            0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    search_ctrl_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(180, -1));
    tb->Add(search_ctrl_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

    tb->Add(make_label(wxString::FromUTF8("排序")),
            0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    sort_choice_ = new wxChoice(this, wxID_ANY);
    sort_choice_->Append(wxString::FromUTF8("默认"));
    sort_choice_->Append(wxString::FromUTF8("按名称"));
    sort_choice_->Append(wxString::FromUTF8("按延迟"));
    sort_choice_->SetSelection(0);
    tb->Add(sort_choice_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

    tb->AddStretchSpacer(1);

    test_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("测速"));
    testall_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("全部测速"));
    refresh_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("刷新"));
    tb->Add(test_btn_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    tb->Add(testall_btn_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    tb->Add(refresh_btn_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);

    outer->Add(tb, 0, wxEXPAND | wxTOP | wxBOTTOM, 8);

    auto* body = new wxBoxSizer(wxHORIZONTAL);
    group_list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition,
                                wxSize(250, -1));
    group_list_->SetBackgroundColour(surface_container_low);
    node_view_ = new NodeListView(this);
    body->Add(group_list_, 0, wxEXPAND | wxLEFT, 8);
    body->Add(node_view_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    outer->Add(body, 1, wxEXPAND);

    status_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    status_text_->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                                 wxFONTWEIGHT_NORMAL));
    status_text_->SetForegroundColour(on_surface_variant);
    outer->Add(status_text_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);

    SetSizer(outer);
    Layout();

    // ---- 事件绑定 ----
    mode_choice_->Bind(wxEVT_CHOICE, &ProxiesPage::OnModeChanged, this);
    sort_choice_->Bind(wxEVT_CHOICE, &ProxiesPage::OnSortChanged, this);
    search_ctrl_->Bind(wxEVT_TEXT, &ProxiesPage::OnSearchChanged, this);
    group_list_->Bind(wxEVT_LISTBOX, &ProxiesPage::OnGroupSelected, this);
    test_btn_->Bind(wxEVT_BUTTON, &ProxiesPage::OnTestSelected, this);
    testall_btn_->Bind(wxEVT_BUTTON, &ProxiesPage::OnTestAll, this);
    refresh_btn_->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent&) { ReloadFromApi(); });

    node_view_->on_node_clicked =
        [this](const wxString& name) { OnNodeClicked(name); };

    // 仅在 AppContext 已初始化时拉取数据；否则留空等待手动刷新。
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        ReloadFromApi();

        auto alive = alive_;
        // 内核启动/重启后刷新（重启后 mihomo 的组状态会重置）
        core_state_handle_ = clash_yuka::AppContext::Get().SubscribeCoreState(
            [this, alive](bool running) {
                if (!running) return;
                wxTheApp->CallAfter([this, alive]() {
                    if (!alive->load(std::memory_order_acquire)) return;
                    ReloadFromApi();
                });
            });
        // 配置应用成功（导入/激活订阅、设置变更）后刷新，无需重启 GUI
        config_applied_handle_ = clash_yuka::AppContext::Get().SubscribeConfigApplied(
            [this, alive]() {
                wxTheApp->CallAfter([this, alive]() {
                    if (!alive->load(std::memory_order_acquire)) return;
                    ReloadFromApi();
                });
            });
    }
}

ProxiesPage::~ProxiesPage() {
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        if (core_state_handle_ != -1) {
            clash_yuka::AppContext::Get().UnsubscribeCoreState(core_state_handle_);
            core_state_handle_ = -1;
        }
        if (config_applied_handle_ != -1) {
            clash_yuka::AppContext::Get().UnsubscribeConfigApplied(config_applied_handle_);
            config_applied_handle_ = -1;
        }
    }
}

void ProxiesPage::SetStatus(const wxString& text, bool is_error) {
    if (!status_text_) return;
    wxString shown = text;
    // 错误信息可能很长（如 mihomo 的 YAML 解析错误），截断显示并保留完整 tooltip
    if (shown.length() > 240) {
        shown = shown.Left(240) + wxString::FromUTF8("…");
    }
    status_text_->SetLabel(shown);
    status_text_->SetToolTip(text);
    status_text_->SetForegroundColour(
        is_error ? wxColour(185, 28, 28) : wxColour(113, 113, 122));
    Layout();
}

void ProxiesPage::ReloadFromApi() {
    if (!clash_yuka::AppContext::Get().IsCoreRunning()) {
        SetStatus(wxString::FromUTF8("内核未运行，无法获取代理数据"), true);
        return;
    }
    auto alive = alive_;
    auto gen = load_gen_;
    const int my_gen = ++(*gen);

    std::thread([this, alive, gen, my_gen]() {
        clash_yuka::clash::ProxySnapshot snapshot;
        // 内核刚启动/刚重载时 /proxies 可能短暂为空，做有限重试（约 9 秒）
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (!alive->load(std::memory_order_acquire)) return;
            if (!clash_yuka::AppContext::Get().IsCoreRunning()) return;
            snapshot = clash_yuka::AppContext::Get().api().GetProxySnapshot();
            if (!snapshot.groups.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        // name -> {delay, type, provider}
        std::unordered_map<std::string, int> delay_map;
        std::unordered_map<std::string, std::string> type_map;
        delay_map.reserve(snapshot.proxies.size());
        type_map.reserve(snapshot.proxies.size());
        for (const auto& p : snapshot.proxies) {
            delay_map.emplace(p.name, p.delay.value_or(-1));
            std::string type = p.type;
            if (!p.provider.empty()) type += " · " + p.provider;
            type_map.emplace(p.name, type);
        }

        std::vector<RawGroup> raw_groups;
        raw_groups.reserve(snapshot.groups.size());
        for (const auto& g : snapshot.groups) {
            RawGroup rg;
            rg.name = g.name;
            rg.type = g.type;
            rg.now = g.now;
            rg.nodes.reserve(g.all.size());
            for (const auto& name : g.all) {
                RawNode node;
                node.name = name;
                const auto tit = type_map.find(name);
                if (tit != type_map.end()) node.type = tit->second;
                const auto dit = delay_map.find(name);
                node.delay = (dit != delay_map.end()) ? dit->second : -1;
                rg.nodes.push_back(std::move(node));
            }
            raw_groups.push_back(std::move(rg));
        }

        std::string cfg = clash_yuka::AppContext::Get().api().GetConfig();
        std::string mode = "rule";
        try {
            const auto j = nlohmann::json::parse(cfg);
            if (j.contains("mode") && j["mode"].is_string()) {
                mode = j["mode"].get<std::string>();
            }
        } catch (...) {
            // 解析失败保持默认 rule
        }

        wxTheApp->CallAfter([this, alive, gen, my_gen,
                             raw_groups, mode]() {
            if (!alive->load(std::memory_order_acquire)) return;
            // 丢弃过期结果：连续导入两份配置时只保留最后一次
            if (my_gen != gen->load(std::memory_order_acquire)) return;

            // 记录刷新前选中的组名，刷新后尽量保持
            wxString keep_group = current_group_name_;
            if (current_group_ >= 0 &&
                current_group_ < static_cast<int>(groups_.size())) {
                keep_group = groups_[current_group_].name;
            }

            groups_.clear();
            groups_.reserve(raw_groups.size());
            for (const auto& rg : raw_groups) {
                GroupInfo gi;
                gi.name = wxString::FromUTF8(rg.name.c_str());
                gi.type = wxString::FromUTF8(rg.type.c_str());
                gi.now = wxString::FromUTF8(rg.now.c_str());
                gi.nodes.reserve(rg.nodes.size());
                for (const auto& rn : rg.nodes) {
                    ProxyNodeEntry e;
                    e.name = wxString::FromUTF8(rn.name.c_str());
                    e.type = wxString::FromUTF8(rn.type.c_str());
                    e.delay = rn.delay;
                    gi.nodes.push_back(std::move(e));
                }
                groups_.push_back(std::move(gi));
            }

            // 模式选择（程序化 SetSelection 不触发 wxEVT_CHOICE）
            const wxString wmode = wxString::FromUTF8(mode.c_str());
            const int m_idx = mode_choice_->FindString(wmode);
            if (m_idx != wxNOT_FOUND) mode_choice_->SetSelection(m_idx);

            group_list_->Clear();
            for (const auto& g : groups_) {
                group_list_->Append(
                    wxString::Format("%s  [%s]", g.name, g.type));
            }

            int new_index = -1;
            if (!keep_group.empty()) {
                for (int i = 0; i < static_cast<int>(groups_.size()); ++i) {
                    if (groups_[i].name.IsSameAs(keep_group)) {
                        new_index = i;
                        break;
                    }
                }
            }
            if (new_index < 0 && !groups_.empty()) new_index = 0;

            current_group_ = new_index;
            current_group_name_.clear();
            if (current_group_ >= 0) {
                group_list_->SetSelection(current_group_);
                current_group_name_ = groups_[current_group_].name;
                ApplyCurrentGroup();
                SetStatus(wxString::FromUTF8(""));
            } else {
                current_node_name_.clear();
                node_view_->SetNodes({}, "");
                SetStatus(wxString::FromUTF8(
                    "未获取到代理组：配置中可能没有 proxy-groups，或内核尚未加载完成"),
                    true);
            }
        });
    }).detach();
}

void ProxiesPage::ApplyCurrentGroup() {
    if (current_group_ < 0 ||
        current_group_ >= static_cast<int>(groups_.size())) {
        node_view_->SetNodes({}, "");
        return;
    }
    const auto& g = groups_[current_group_];
    current_node_name_ = g.now;
    // 保留当前搜索/排序设置（NodeListView 内部 filter_/sort_mode_ 持续生效）
    node_view_->SetNodes(g.nodes, g.now);
}

void ProxiesPage::OnGroupSelected(wxCommandEvent&) {
    const int idx = group_list_->GetSelection();
    if (idx < 0 || idx >= static_cast<int>(groups_.size())) return;
    current_group_ = idx;
    current_group_name_ = groups_[idx].name;
    ApplyCurrentGroup();
}

void ProxiesPage::OnNodeClicked(const wxString& node_name) {
    if (current_group_ < 0 ||
        current_group_ >= static_cast<int>(groups_.size())) return;

    const std::string gname = ToUtf8(groups_[current_group_].name);
    const std::string nname = ToUtf8(node_name);
    const wxString group_type = groups_[current_group_].type;
    if (node_name.IsSameAs(groups_[current_group_].now)) return;

    SetStatus(wxString::FromUTF8("正在切换节点…"));

    // 不在这里做乐观更新：以 mihomo 的返回结果 + 刷新后的 now 为准，
    // 避免切换失败时界面显示成已切换。
    auto alive = alive_;
    std::thread([this, alive, gname, nname, group_type]() {
        if (!alive->load(std::memory_order_acquire)) return;
        auto result = clash_yuka::AppContext::Get().api().SelectProxy(gname, nname);
        wxTheApp->CallAfter([this, alive, result, nname, group_type]() {
            if (!alive->load(std::memory_order_acquire)) return;
            const wxString wname = wxString::FromUTF8(nname.c_str());
            if (!result.ok) {
                // 保留 mihomo 原始错误（如「Selector is required」），不做统一模糊化
                SetStatus(wxString::FromUTF8("切换失败: ") +
                          wxString::FromUTF8(result.error.c_str()), true);
                return;
            }
            wxString msg = wxString::FromUTF8("已切换到 ") + wname;
            if (IsAutoGroupType(group_type)) {
                msg += wxString::FromUTF8("（该组为自动选择组，手动选择会固定节点）");
            }
            SetStatus(msg);
            // 以 /proxies 的 now 为准刷新高亮与列表
            ReloadFromApi();
        });
    }).detach();
}

void ProxiesPage::OnTestSelected(wxCommandEvent&) {
    if (current_group_ < 0 ||
        current_group_ >= static_cast<int>(groups_.size())) return;
    if (current_node_name_.empty()) {
        SetStatus(wxString::FromUTF8("当前组没有可测速的节点"), true);
        return;
    }

    const std::string nname = ToUtf8(current_node_name_);
    const std::string url = TestUrl();
    const int timeout = TestTimeout();
    SetStatus(wxString::FromUTF8("正在测速 ") + current_node_name_ +
              wxString::FromUTF8(" …"));

    auto alive = alive_;
    std::thread([this, alive, nname, url, timeout]() {
        if (!alive->load(std::memory_order_acquire)) return;
        auto result = clash_yuka::AppContext::Get().api()
                          .GetProxyDelay(nname, url, timeout);
        wxTheApp->CallAfter([this, alive, result, nname]() {
            if (!alive->load(std::memory_order_acquire)) return;
            const wxString wname = wxString::FromUTF8(nname.c_str());
            if (!result.ok) {
                // 失败时 chip 显示 timeout，并在状态栏给出具体原因
                node_view_->UpdateDelay(wname, 0);
                if (current_group_ >= 0 &&
                    current_group_ < static_cast<int>(groups_.size())) {
                    for (auto& e : groups_[current_group_].nodes) {
                        if (e.name.IsSameAs(wname)) { e.delay = 0; break; }
                    }
                }
                SetStatus(wname + wxString::FromUTF8(" 测速失败: ") +
                          wxString::FromUTF8(result.error.c_str()), true);
                return;
            }
            node_view_->UpdateDelay(wname, result.delay_ms);
            if (current_group_ >= 0 &&
                current_group_ < static_cast<int>(groups_.size())) {
                for (auto& e : groups_[current_group_].nodes) {
                    if (e.name.IsSameAs(wname)) { e.delay = result.delay_ms; break; }
                }
            }
            SetStatus(wname + wxString::Format(": %dms", result.delay_ms));
        });
    }).detach();
}

void ProxiesPage::OnTestAll(wxCommandEvent&) {
    if (current_group_ < 0 ||
        current_group_ >= static_cast<int>(groups_.size())) return;

    const std::string gname = ToUtf8(groups_[current_group_].name);
    std::vector<std::string> names;
    names.reserve(groups_[current_group_].nodes.size());
    for (const auto& e : groups_[current_group_].nodes) {
        names.push_back(ToUtf8(e.name));
    }
    if (names.empty()) {
        SetStatus(wxString::FromUTF8("当前组没有可测速的节点"), true);
        return;
    }

    const std::string url = TestUrl();
    const int timeout = TestTimeout();
    SetStatus(wxString::FromUTF8("正在测速整组…"));

    auto alive = alive_;
    // 说明：整组测速使用 GET /group/{name}/delay，由 mihomo 内部并行测试组内成员，
    // 不会调用 SelectProxy，因此不会改变用户当前的代理选择。
    std::thread([this, alive, gname, names, url, timeout]() {
        if (!alive->load(std::memory_order_acquire)) return;
        auto group_result = clash_yuka::AppContext::Get().api()
                                .GetGroupDelay(gname, url, timeout);

        if (group_result.ok) {
            std::unordered_map<std::string, int> delays;
            delays.reserve(group_result.delays.size());
            for (const auto& kv : group_result.delays) delays.emplace(kv.first, kv.second);

            wxTheApp->CallAfter([this, alive, names, delays]() {
                if (!alive->load(std::memory_order_acquire)) return;
                int ok_count = 0;
                for (const auto& n : names) {
                    const auto it = delays.find(n);
                    const int d = (it != delays.end()) ? it->second : 0;
                    if (it != delays.end()) ++ok_count;
                    const wxString wname = wxString::FromUTF8(n.c_str());
                    node_view_->UpdateDelay(wname, d);
                    if (current_group_ >= 0 &&
                        current_group_ < static_cast<int>(groups_.size())) {
                        for (auto& e : groups_[current_group_].nodes) {
                            if (e.name.IsSameAs(wname)) { e.delay = d; break; }
                        }
                    }
                }
                SetStatus(wxString::Format(
                    wxString::FromUTF8("组测速完成：%d/%d 个节点可用"),
                    ok_count, static_cast<int>(names.size())));
            });
            return;
        }

        // 组测速失败（如 Relay 组不支持 /group 接口）：如实提示后逐节点回退
        const wxString group_error = wxString::FromUTF8(group_result.error.c_str());
        wxTheApp->CallAfter([this, alive, group_error]() {
            if (!alive->load(std::memory_order_acquire)) return;
            SetStatus(wxString::FromUTF8("组测速失败（") + group_error +
                      wxString::FromUTF8("），改为逐节点测速…"), true);
        });

        for (const auto& n : names) {
            if (!alive->load(std::memory_order_acquire)) return;
            auto result = clash_yuka::AppContext::Get().api()
                              .GetProxyDelay(n, url, timeout);
            wxTheApp->CallAfter([this, alive, result, n]() {
                if (!alive->load(std::memory_order_acquire)) return;
                const wxString wname = wxString::FromUTF8(n.c_str());
                const int d = result.ok ? result.delay_ms : 0;
                node_view_->UpdateDelay(wname, d);
                if (current_group_ >= 0 &&
                    current_group_ < static_cast<int>(groups_.size())) {
                    for (auto& e : groups_[current_group_].nodes) {
                        if (e.name.IsSameAs(wname)) { e.delay = d; break; }
                    }
                }
            });
        }
    }).detach();
}

void ProxiesPage::OnSearchChanged(wxCommandEvent&) {
    node_view_->SetFilter(search_ctrl_->GetValue());
}

void ProxiesPage::OnSortChanged(wxCommandEvent& event) {
    ProxySortMode mode = ProxySortMode::Default;
    switch (event.GetSelection()) {
        case 1: mode = ProxySortMode::ByName; break;
        case 2: mode = ProxySortMode::ByDelay; break;
        default: break;
    }
    node_view_->SetSortMode(mode);
}

void ProxiesPage::OnModeChanged(wxCommandEvent& event) {
    const std::string m = ToUtf8(event.GetString());
    auto alive = alive_;
    std::thread([alive, m]() {
        if (!alive->load(std::memory_order_acquire)) return;
        nlohmann::json patch = {{"mode", m}};
        clash_yuka::AppContext::Get().api().PatchConfig(patch);
    }).detach();
}

std::string ProxiesPage::TestUrl() const {
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        const auto& v = clash_yuka::AppContext::Get().verge();
        if (v.default_latency_test.has_value() &&
            !v.default_latency_test->empty()) {
            return *v.default_latency_test;
        }
    }
    return "https://www.gstatic.com/generate_204";
}

int ProxiesPage::TestTimeout() const {
    return 5000;
}

} // namespace clash_yuka::ui
