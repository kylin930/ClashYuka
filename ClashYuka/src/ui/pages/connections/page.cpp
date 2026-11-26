// ClashYuka Connections 页面实现（Phase 6 Task 12）
//
// 数据流：
//   - ws /connections 后台回调 -> 拷贝 -> wxTheApp->CallAfter -> 主线程 ApplyConnections
//   - 关闭连接：detached 线程调 api.CloseConnection / CloseAllConnections
//   - 所有 UI 访问均发生在主线程；alive_ 标志位避免析构后悬空
#include "ui/pages/connections/page.h"

#include <wx/menu.h>
#include <wx/sizer.h>

#include <algorithm>
#include <optional>
#include <set>
#include <thread>
#include <utility>

#include "core/app_context.h"
#include "core/clash/types.h"

namespace clash_yuka::ui {

namespace {

// 控件命令 ID（事件表与控件创建共用）
constexpr int ID_SEARCH = wxID_HIGHEST + 210;
constexpr int ID_FILTER_LIST = wxID_HIGHEST + 211;
constexpr int ID_LIST = wxID_HIGHEST + 212;
constexpr int ID_CLOSE_CONNECTION = wxID_HIGHEST + 200;
constexpr int ID_CLOSE_ALL = wxID_HIGHEST + 201;

// 字节数格式化（B/KB/MB/GB）
wxString FormatBytes(int64_t bytes) {
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    if (bytes <= 0) return "0 B";
    double v = static_cast<double>(bytes);
    if (v < kKB) return wxString::Format("%lld B", static_cast<long long>(bytes));
    if (v < kMB) return wxString::Format("%.2f KB", v / kKB);
    if (v < kGB) return wxString::Format("%.2f MB", v / kMB);
    return wxString::Format("%.2f GB", v / kGB);
}

// 大小写不敏感子串匹配
bool ContainsCI(const wxString& hay, const wxString& needle) {
    if (needle.empty()) return true;
    return hay.Lower().Contains(needle.Lower());
}

// 拆分逗号分隔的 chains 字符串并去除首尾空白
std::vector<std::string> SplitChains(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        size_t end = (comma == std::string::npos) ? s.size() : comma;
        std::string token = s.substr(start, end - start);
        size_t b = token.find_first_not_of(" \t");
        size_t e = token.find_last_not_of(" \t");
        if (b != std::string::npos) out.push_back(token.substr(b, e - b + 1));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

// optional<string> -> wxString（空返回空串）
wxString OptStr(const std::optional<std::string>& s) {
    return s ? wxString::FromUTF8(*s) : wxString();
}

} // namespace

wxBEGIN_EVENT_TABLE(ConnectionsPage, wxPanel)
    EVT_TEXT(ID_SEARCH, ConnectionsPage::OnSearch)
    EVT_LISTBOX(ID_FILTER_LIST, ConnectionsPage::OnFilterSelected)
    EVT_LIST_COL_CLICK(ID_LIST, ConnectionsPage::OnColClick)
    EVT_LIST_ITEM_RIGHT_CLICK(ID_LIST, ConnectionsPage::OnItemRightClick)
    EVT_MENU(ID_CLOSE_CONNECTION, ConnectionsPage::OnCloseConnectionMenu)
    EVT_BUTTON(ID_CLOSE_ALL, ConnectionsPage::OnCloseAll)
wxEND_EVENT_TABLE()

// ============================================================
// ConnectionsListCtrl
// ============================================================

ConnectionsListCtrl::ConnectionsListCtrl(wxWindow* parent, wxWindowID id,
                                         ConnectionsPage* page)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL |
                 wxLC_HRULES | wxLC_VRULES),
      page_(page) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

    alt_attr_ = std::make_unique<wxListItemAttr>();
    alt_attr_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

    InsertColumn(ConnectionsPage::Col_Host,     wxString::FromUTF8("Host"),     wxLIST_FORMAT_LEFT, 200);
    InsertColumn(ConnectionsPage::Col_Network,  wxString::FromUTF8("网络"),     wxLIST_FORMAT_LEFT, 60);
    InsertColumn(ConnectionsPage::Col_Type,     wxString::FromUTF8("类型"),     wxLIST_FORMAT_LEFT, 80);
    InsertColumn(ConnectionsPage::Col_Source,   wxString::FromUTF8("源地址"),   wxLIST_FORMAT_LEFT, 160);
    InsertColumn(ConnectionsPage::Col_Dest,     wxString::FromUTF8("目标地址"), wxLIST_FORMAT_LEFT, 160);
    InsertColumn(ConnectionsPage::Col_Chains,   wxString::FromUTF8("代理链"),   wxLIST_FORMAT_LEFT, 140);
    InsertColumn(ConnectionsPage::Col_Rule,     wxString::FromUTF8("规则"),     wxLIST_FORMAT_LEFT, 120);
    InsertColumn(ConnectionsPage::Col_Upload,   wxString::FromUTF8("上传"),     wxLIST_FORMAT_RIGHT, 90);
    InsertColumn(ConnectionsPage::Col_Download, wxString::FromUTF8("下载"),     wxLIST_FORMAT_RIGHT, 90);
    SetItemCount(0);
}

wxString ConnectionsListCtrl::OnGetItemText(long item, long column) const {
    if (!page_ || item < 0 || item >= (long)page_->filtered_view_.size())
        return wxEmptyString;
    const clash::Connection& c = *page_->filtered_view_[item];
    switch (column) {
        case ConnectionsPage::Col_Host:
            return OptStr(c.host);
        case ConnectionsPage::Col_Network:
            return OptStr(c.network);
        case ConnectionsPage::Col_Type:
            return OptStr(c.type);
        case ConnectionsPage::Col_Source: {
            wxString ip = OptStr(c.sourceIP), port = OptStr(c.sourcePort);
            if (ip.empty()) return port;
            if (port.empty()) return ip;
            return ip + ":" + port;
        }
        case ConnectionsPage::Col_Dest: {
            wxString ip = OptStr(c.destinationIP), port = OptStr(c.destinationPort);
            if (ip.empty()) return port;
            if (port.empty()) return ip;
            return ip + ":" + port;
        }
        case ConnectionsPage::Col_Chains:
            return OptStr(c.chains);
        case ConnectionsPage::Col_Rule: {
            wxString r = OptStr(c.rule), p = OptStr(c.rulePayload);
            if (!p.empty()) return r.empty() ? p : (r + " (" + p + ")");
            return r;
        }
        case ConnectionsPage::Col_Upload:
            return FormatBytes(c.upload.value_or(0));
        case ConnectionsPage::Col_Download:
            return FormatBytes(c.download.value_or(0));
        default:
            return wxEmptyString;
    }
}

wxListItemAttr* ConnectionsListCtrl::OnGetItemAttr(long item) const {
    // 交替行底色，提升大量行时的可读性
    if (item % 2 == 1) return alt_attr_.get();
    return nullptr;
}

// ============================================================
// ConnectionsPage
// ============================================================

ConnectionsPage::ConnectionsPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ---- 工具条 ----
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    search_box_ = new wxTextCtrl(this, ID_SEARCH, wxEmptyString,
                                 wxDefaultPosition, wxSize(280, -1));
    search_box_->SetHint(wxString::FromUTF8("搜索 Host / IP / 链等"));
    toolbar->Add(search_box_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

    count_label_ = new wxStaticText(this, wxID_ANY, "0 / 0");
    count_label_->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    count_label_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    toolbar->Add(count_label_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);

    toolbar->AddStretchSpacer(1);

    close_all_btn_ = new wxButton(this, ID_CLOSE_ALL,
                                  wxString::FromUTF8("关闭全部连接"));
    toolbar->Add(close_all_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    outer->Add(toolbar, 0, wxEXPAND | wxTOP | wxBOTTOM, 6);

    // ---- 主体：侧栏 + 列表 ----
    auto* body = new wxBoxSizer(wxHORIZONTAL);

    // 侧栏：按代理链筛选
    auto* side = new wxPanel(this);
    side->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
    auto* sideSizer = new wxBoxSizer(wxVERTICAL);
    filter_title_ = new wxStaticText(side, wxID_ANY,
                                      wxString::FromUTF8("按代理链筛选"));
    filter_title_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    filter_title_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sideSizer->Add(filter_title_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 10);

    filter_list_ = new wxListBox(side, ID_FILTER_LIST,
                                 wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                 wxLB_SINGLE | wxBORDER_NONE);
    filter_list_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
    filter_list_->Append(wxString::FromUTF8("全部"));
    filter_list_->SetSelection(0);
    chains_.push_back("全部");
    sideSizer->Add(filter_list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    side->SetSizer(sideSizer);
    side->SetMinSize(wxSize(180, -1));
    body->Add(side, 0, wxEXPAND);

    // 虚拟列表
    list_ctrl_ = new ConnectionsListCtrl(this, ID_LIST, this);
    body->Add(list_ctrl_, 1, wxEXPAND);

    outer->Add(body, 1, wxEXPAND);
    SetSizer(outer);
    Layout();

    // 仅在 AppContext 已初始化时注册回调；未初始化（如调试场景）静默跳过
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        RegisterCallbacks();
    }
}

ConnectionsPage::~ConnectionsPage() {
    UnregisterCallbacks();
}

void ConnectionsPage::RegisterCallbacks() {
    auto& ctx = clash_yuka::AppContext::Get();
    auto alive = alive_;  // shared_ptr 拷贝，延长 bool 生命周期

    // /connections 回调：参数为 (连接列表, total)。实时刷新连接表格。
    // 使用多订阅者 API：与 DashboardPage 共享 /connections 流，互不覆盖。
    connections_handle_ = ctx.ws().SubscribeConnections(
        [this, alive](const std::vector<clash::Connection>& conns, int /*total*/) {
            // ws 回调在后台线程：先拷贝数据，再通过 CallAfter 投递到主线程
            std::vector<clash::Connection> copy = conns;
            wxTheApp->CallAfter([this, alive, copy = std::move(copy)]() mutable {
                if (!alive->load(std::memory_order_acquire)) return;
                ApplyConnections(std::move(copy));
            });
        });
}

void ConnectionsPage::UnregisterCallbacks() {
    // 先标记 alive_=false，使已投递但未执行的 CallAfter 跳过 UI 访问；
    // 再注销 ws 订阅，阻止后台线程继续投递新事件。
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        if (connections_handle_ != -1) {
            clash_yuka::AppContext::Get().ws().UnsubscribeConnections(connections_handle_);
            connections_handle_ = -1;
        }
    }
}

void ConnectionsPage::ApplyConnections(std::vector<clash::Connection> conns) {
    all_connections_ = std::move(conns);
    RebuildChainsSidebar();
    RebuildFilteredView();
}

void ConnectionsPage::RebuildChainsSidebar() {
    // 收集所有唯一 chain（拆分逗号分隔的 chains 字段）
    std::set<std::string> unique;
    for (const auto& c : all_connections_) {
        if (!c.chains) continue;
        for (const auto& p : SplitChains(*c.chains)) unique.insert(p);
    }

    // 计算签名；无变化则跳过重建（避免每秒刷新导致 ListBox 闪烁）
    std::string sig;
    for (const auto& c : unique) sig += c + "|";
    if (sig == chains_signature_) return;
    chains_signature_ = sig;

    // 记住当前选中，重建后尽量恢复
    wxString prev_sel;
    if (selected_chain_idx_ >= 0 && selected_chain_idx_ < (int)chains_.size()) {
        prev_sel = wxString::FromUTF8(chains_[selected_chain_idx_]);
    }

    chains_.clear();
    chains_.push_back("全部");
    for (const auto& c : unique) chains_.push_back(c);

    filter_list_->Clear();
    for (const auto& c : chains_) filter_list_->Append(wxString::FromUTF8(c));

    int new_sel = 0;
    if (!prev_sel.empty()) {
        for (size_t i = 0; i < chains_.size(); ++i) {
            if (wxString::FromUTF8(chains_[i]) == prev_sel) {
                new_sel = (int)i;
                break;
            }
        }
    }
    selected_chain_idx_ = new_sel;
    filter_list_->SetSelection(new_sel);
}

void ConnectionsPage::RebuildFilteredView() {
    // 当前选中的 chain（"全部" 时不筛选）
    const std::string* chain_filter = nullptr;
    if (selected_chain_idx_ > 0 && selected_chain_idx_ < (int)chains_.size()) {
        chain_filter = &chains_[selected_chain_idx_];
    }

    filtered_view_.clear();
    filtered_view_.reserve(all_connections_.size());
    for (const auto& c : all_connections_) {
        // chain 筛选
        if (chain_filter) {
            bool match = false;
            if (c.chains) {
                for (const auto& p : SplitChains(*c.chains)) {
                    if (p == *chain_filter) { match = true; break; }
                }
            }
            if (!match) continue;
        }
        // 搜索筛选（host / sourceIP / destinationIP / chains / rule / type / network）
        if (!search_text_.empty()) {
            wxString hay;
            hay << OptStr(c.host) << " " << OptStr(c.sourceIP) << " "
                << OptStr(c.destinationIP) << " " << OptStr(c.chains) << " "
                << OptStr(c.rule) << " " << OptStr(c.rulePayload) << " "
                << OptStr(c.type) << " " << OptStr(c.network);
            if (!ContainsCI(hay, search_text_)) continue;
        }
        filtered_view_.push_back(&c);
    }

    // 排序（点击列头切换升降序）
    if (sort_col_ >= 0) {
        int col = sort_col_;
        bool asc = sort_asc_;
        std::sort(filtered_view_.begin(), filtered_view_.end(),
            [col, asc](const clash::Connection* a, const clash::Connection* b) {
                int cmp = 0;
                switch (col) {
                    case ConnectionsPage::Col_Host:
                        cmp = OptStr(a->host).CmpNoCase(OptStr(b->host)); break;
                    case ConnectionsPage::Col_Network:
                        cmp = OptStr(a->network).CmpNoCase(OptStr(b->network)); break;
                    case ConnectionsPage::Col_Type:
                        cmp = OptStr(a->type).CmpNoCase(OptStr(b->type)); break;
                    case ConnectionsPage::Col_Source:
                        cmp = (OptStr(a->sourceIP) + ":" + OptStr(a->sourcePort))
                              .CmpNoCase(OptStr(b->sourceIP) + ":" + OptStr(b->sourcePort));
                        break;
                    case ConnectionsPage::Col_Dest:
                        cmp = (OptStr(a->destinationIP) + ":" + OptStr(a->destinationPort))
                              .CmpNoCase(OptStr(b->destinationIP) + ":" + OptStr(b->destinationPort));
                        break;
                    case ConnectionsPage::Col_Chains:
                        cmp = OptStr(a->chains).CmpNoCase(OptStr(b->chains)); break;
                    case ConnectionsPage::Col_Rule:
                        cmp = OptStr(a->rule).CmpNoCase(OptStr(b->rule)); break;
                    case ConnectionsPage::Col_Upload: {
                        int64_t va = a->upload.value_or(0), vb = b->upload.value_or(0);
                        cmp = (va < vb) ? -1 : (va > vb) ? 1 : 0;
                        break;
                    }
                    case ConnectionsPage::Col_Download: {
                        int64_t va = a->download.value_or(0), vb = b->download.value_or(0);
                        cmp = (va < vb) ? -1 : (va > vb) ? 1 : 0;
                        break;
                    }
                    default: break;
                }
                return asc ? (cmp < 0) : (cmp > 0);
            });
    }

    list_ctrl_->SetItemCount(static_cast<long>(filtered_view_.size()));
    list_ctrl_->Refresh();

    count_label_->SetLabel(wxString::Format("%d / %d",
        (int)filtered_view_.size(), (int)all_connections_.size()));
}

void ConnectionsPage::OnSearch(wxCommandEvent&) {
    search_text_ = search_box_->GetValue();
    RebuildFilteredView();
}

void ConnectionsPage::OnFilterSelected(wxCommandEvent& event) {
    selected_chain_idx_ = event.GetInt();
    RebuildFilteredView();
}

void ConnectionsPage::OnColClick(wxListEvent& event) {
    int col = event.GetColumn();
    if (col < 0 || col > ConnectionsPage::Col_Download) return;
    if (sort_col_ == col) {
        sort_asc_ = !sort_asc_;
    } else {
        sort_col_ = col;
        sort_asc_ = true;
    }
    RebuildFilteredView();
}

void ConnectionsPage::OnItemRightClick(wxListEvent& event) {
    long idx = event.GetIndex();
    if (idx < 0 || idx >= (long)filtered_view_.size()) return;
    context_conn_id_ = filtered_view_[idx]->id;
    wxMenu menu;
    menu.Append(ID_CLOSE_CONNECTION, wxString::FromUTF8("关闭此连接"));
    PopupMenu(&menu);
}

void ConnectionsPage::OnCloseConnectionMenu(wxCommandEvent&) {
    if (!context_conn_id_.empty()) {
        DoCloseConnection(context_conn_id_);
        context_conn_id_.clear();
    }
}

void ConnectionsPage::OnCloseAll(wxCommandEvent&) {
    DoCloseAllConnections();
}

void ConnectionsPage::DoCloseConnection(const std::string& id) {
    if (!clash_yuka::AppContext::Get().IsInitialized()) return;
    auto alive = alive_;
    std::string conn_id = id;
    // detached 线程执行同步 HTTP 调用，避免阻塞 UI 主线程。
    // 关闭后 ws 会自动推送新的连接列表，无需手动刷新。
    std::thread([alive, conn_id]() {
        if (!alive->load(std::memory_order_acquire)) return;
        clash_yuka::AppContext::Get().api().CloseConnection(conn_id);
    }).detach();
}

void ConnectionsPage::DoCloseAllConnections() {
    if (!clash_yuka::AppContext::Get().IsInitialized()) return;
    auto alive = alive_;
    std::thread([alive]() {
        if (!alive->load(std::memory_order_acquire)) return;
        clash_yuka::AppContext::Get().api().CloseAllConnections();
    }).detach();
}

} // namespace clash_yuka::ui
