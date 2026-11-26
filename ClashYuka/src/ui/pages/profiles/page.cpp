// ClashYuka Profiles 页面实现（Phase 7 Task 16）
#include "ui/pages/profiles/page.h"

#include <wx/arrstr.h>
#include <wx/choicdlg.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#include <ixwebsocket/IXHttpClient.h>

#include "core/app_context.h"
#include "core/utils/dirs.h"

namespace clash_yuka::ui {

namespace {

// wxString -> std::string（UTF-8）
std::string ToUtf8(const wxString& s) {
    const char* p = s.ToUTF8().data();
    return p ? std::string(p) : std::string();
}

// 创建只读 label StaticText
wxStaticText* MakeLabel(wxWindow* parent, const wxString& text) {
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return t;
}

// 创建只读 value StaticText（占位 "--"）
wxStaticText* MakeValue(wxWindow* parent) {
    auto* t = new wxStaticText(parent, wxID_ANY, "--");
    t->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    return t;
}

// 字节数 -> 人类可读（KB/MB/GB）
wxString FormatBytes(uint64_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    if (bytes >= GB) return wxString::Format("%.2f GB", bytes / GB);
    if (bytes >= MB) return wxString::Format("%.2f MB", bytes / MB);
    if (bytes >= KB) return wxString::Format("%.2f KB", bytes / KB);
    return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
}

// Unix 时间戳 -> "YYYY-MM-DD HH:MM"
wxString FormatTime(uint64_t ts) {
    if (ts == 0) return wxString::FromUTF8("N/A");
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M");
    return wxString::FromUTF8(oss.str());
}

// 解析 Subscription-Userinfo 头：upload=..; download=..; total=..; expire=..
std::optional<config::SubscriptionInfo> ParseSubscriptionInfo(const std::string& header) {
    if (header.empty()) return std::nullopt;
    config::SubscriptionInfo info{};
    bool any = false;
    std::istringstream iss(header);
    std::string token;
    while (std::getline(iss, token, ';')) {
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        token = token.substr(start, end - start + 1);
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);
        try {
            uint64_t v = std::stoull(val);
            if (key == "upload") { info.upload = v; any = true; }
            else if (key == "download") { info.download = v; any = true; }
            else if (key == "total") { info.total = v; any = true; }
            else if (key == "expire") { info.expire = v; any = true; }
        } catch (...) {
            // 忽略解析失败的数值
        }
    }
    return any ? std::optional<config::SubscriptionInfo>(info) : std::nullopt;
}

// 类型 -> 中文标题
wxString ProfileTypeLabel(config::ProfileType t) {
    switch (t) {
        case config::ProfileType::Remote: return wxString::FromUTF8("远程订阅");
        case config::ProfileType::Local:  return wxString::FromUTF8("本地配置");
        case config::ProfileType::Merge:  return wxString::FromUTF8("合并配置");
        case config::ProfileType::Script: return wxString::FromUTF8("脚本配置");
    }
    return "Unknown";
}

// 类型索引：0=Remote 1=Local 2=Merge 3=Script
int TypeIndex(config::ProfileType t) {
    switch (t) {
        case config::ProfileType::Remote: return 0;
        case config::ProfileType::Local:  return 1;
        case config::ProfileType::Merge:  return 2;
        case config::ProfileType::Script: return 3;
    }
    return 0;
}

// 当前 Unix 时间戳（秒）
uint64_t NowSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

// ============================================================
// ProfilesPage
// ============================================================

ProfilesPage::ProfilesPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ---- 顶部工具栏 ----
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);

    import_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("导入订阅"));
    import_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnImportRemote, this);
    toolbar->Add(import_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    new_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("新建"));
    new_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnNewProfile, this);
    toolbar->Add(new_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    refresh_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("刷新"));
    refresh_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnRefresh, this);
    toolbar->Add(refresh_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    toolbar->AddStretchSpacer(1);

    activate_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("激活当前"));
    activate_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnActivateCurrent, this);
    toolbar->Add(activate_btn_, 0, wxALIGN_CENTER_VERTICAL);

    outer->Add(toolbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    // ---- 主体：左树 + 右详情 ----
    auto* body = new wxBoxSizer(wxHORIZONTAL);

    tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition,
                           wxSize(280, -1),
                           wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_NO_LINES);
    tree_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    tree_->Bind(wxEVT_TREE_SEL_CHANGED, &ProfilesPage::OnTreeSelected, this);
    body->Add(tree_, 0, wxEXPAND | wxRIGHT, 8);

    // 右侧：可滚动的详情 Panel
    detail_scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    detail_scroll_->SetScrollRate(5, 5);
    detail_scroll_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* detail_sizer = new wxBoxSizer(wxVERTICAL);
    detail_scroll_->SetSizer(detail_sizer);

    detail_card_ = new wxPanel(detail_scroll_, wxID_ANY,
                               wxDefaultPosition, wxDefaultSize,
                               wxSIMPLE_BORDER);
    detail_card_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    {
        auto* card_sizer = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(detail_card_, wxID_ANY,
            wxString::FromUTF8("订阅详情"));
        title->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        card_sizer->Add(title, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("名称")),
                        0, wxLEFT | wxTOP, 12);
        name_edit_ = new wxTextCtrl(detail_card_, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, 28));
        card_sizer->Add(name_edit_, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("类型")),
                        0, wxLEFT | wxTOP, 12);
        type_value_ = MakeValue(detail_card_);
        card_sizer->Add(type_value_, 0, wxLEFT | wxRIGHT, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("URL")),
                        0, wxLEFT | wxTOP, 12);
        url_edit_ = new wxTextCtrl(detail_card_, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, 28));
        card_sizer->Add(url_edit_, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("更新时间")),
                        0, wxLEFT | wxTOP, 12);
        updated_value_ = MakeValue(detail_card_);
        card_sizer->Add(updated_value_, 0, wxLEFT | wxRIGHT, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("订阅信息")),
                        0, wxLEFT | wxTOP, 12);
        subinfo_value_ = MakeValue(detail_card_);
        card_sizer->Add(subinfo_value_, 0, wxLEFT | wxRIGHT, 16);

        card_sizer->Add(MakeLabel(detail_card_, wxString::FromUTF8("处理链")),
                        0, wxLEFT | wxTOP, 12);
        chain_list_ = new wxListBox(detail_card_, wxID_ANY, wxDefaultPosition,
                                    wxSize(-1, 100));
        card_sizer->Add(chain_list_, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
        auto* chain_btn_row = new wxBoxSizer(wxHORIZONTAL);
        chain_add_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("添加"));
        chain_add_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnChainAdd, this);
        chain_btn_row->Add(chain_add_btn_, 0, wxRIGHT, 8);
        chain_remove_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("删除"));
        chain_remove_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnChainRemove, this);
        chain_btn_row->Add(chain_remove_btn_, 0, wxRIGHT, 8);
        card_sizer->Add(chain_btn_row, 0, wxLEFT | wxTOP, 8);

        // 操作按钮行
        auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
        save_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("保存"));
        save_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnSave, this);
        btn_row->Add(save_btn_, 0, wxRIGHT, 8);

        delete_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("删除"));
        delete_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnDelete, this);
        btn_row->Add(delete_btn_, 0, wxRIGHT, 8);

        duplicate_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("复制"));
        duplicate_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnDuplicate, this);
        btn_row->Add(duplicate_btn_, 0, wxRIGHT, 8);

        toggle_activate_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("激活"));
        toggle_activate_btn_->Bind(wxEVT_BUTTON,
            &ProfilesPage::OnToggleActivate, this);
        btn_row->Add(toggle_activate_btn_, 0, wxRIGHT, 8);

        up_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("上移"));
        up_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnMoveUp, this);
        btn_row->Add(up_btn_, 0, wxRIGHT, 8);

        down_btn_ = new wxButton(detail_card_, wxID_ANY, wxString::FromUTF8("下移"));
        down_btn_->Bind(wxEVT_BUTTON, &ProfilesPage::OnMoveDown, this);
        btn_row->Add(down_btn_, 0, wxRIGHT, 8);

        card_sizer->Add(btn_row, 0, wxLEFT | wxTOP, 16);

        status_text_ = new wxStaticText(detail_card_, wxID_ANY,
            wxString::FromUTF8("就绪"));
        status_text_->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        status_text_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        card_sizer->Add(status_text_, 0, wxALIGN_LEFT | wxLEFT | wxTOP | wxBOTTOM, 16);

        detail_card_->SetSizer(card_sizer);
        detail_card_->SetMinSize(wxSize(360, 420));
    }
    detail_sizer->Add(detail_card_, 1, wxEXPAND);
    body->Add(detail_scroll_, 1, wxEXPAND);

    outer->Add(body, 1, wxEXPAND | wxALL, 8);
    SetSizer(outer);
    Layout();

    if (clash_yuka::AppContext::Get().IsInitialized()) {
        RebuildTree();
        RefreshActivateButton();
    }
}

ProfilesPage::~ProfilesPage() {
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
}

// ============================================================
// 事件处理与辅助方法实现
// ============================================================

// ---- 工具栏事件 ----

void ProfilesPage::OnImportRemote(wxCommandEvent&) {
    // 1. 输入订阅 URL
    wxTextEntryDialog url_dlg(this, wxString::FromUTF8("请输入订阅 URL"),
                              wxString::FromUTF8("导入远程订阅"));
    if (url_dlg.ShowModal() != wxID_OK) return;
    wxString url = url_dlg.GetValue();
    if (url.empty()) return;

    // 2. 输入名称（可留空）
    wxTextEntryDialog name_dlg(this,
                               wxString::FromUTF8("请输入订阅名称（可留空）"),
                               wxString::FromUTF8("导入远程订阅"));
    if (name_dlg.ShowModal() != wxID_OK) return;

    SetStatus(wxString::FromUTF8("下载中…"));

    auto alive = alive_;
    std::string url_utf8 = ToUtf8(url);
    std::string name_utf8 = ToUtf8(name_dlg.GetValue());

    // 3. 后台线程下载订阅
    std::thread([this, alive, url_utf8, name_utf8]() {
        if (!alive->load(std::memory_order_acquire)) return;

        ix::HttpClient client;
        auto args = client.createRequest();
        args->extraHeaders["User-Agent"] = "clash-verge/v2.0.0";
        args->connectTimeout = 30;
        args->transferTimeout = 60;
        auto response = client.get(url_utf8, args);

        bool ok = response && response->statusCode == 200;
        std::string body = ok ? response->body : std::string();
        std::string subinfo_header;
        int status_code = response ? response->statusCode : 0;
        if (ok) {
            auto it = response->headers.find("Subscription-Userinfo");
            if (it != response->headers.end()) {
                subinfo_header = it->second;
            }
        }

        wxTheApp->CallAfter([this, alive, ok, body, subinfo_header,
                             status_code, url_utf8, name_utf8]() {
            if (!alive->load(std::memory_order_acquire)) return;
            if (!ok) {
                SetStatus(wxString::Format(
                    wxString::FromUTF8("下载失败（状态码 %d）"), status_code));
                return;
            }

            // 生成 uid 并写入文件
            std::string uid =
                config::GenerateProfileUid(config::ProfileType::Remote);
            auto file_path = utils::profiles_dir() / (uid + ".yaml");
            std::ofstream ofs(file_path, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                SetStatus(wxString::FromUTF8("无法写入文件"));
                return;
            }
            ofs << body;
            ofs.close();

            config::Profile p;
            p.uid = uid;
            p.type = config::ProfileType::Remote;
            p.name = name_utf8.empty() ? url_utf8 : name_utf8;
            p.file = uid + ".yaml";
            p.url = url_utf8;
            p.updated = NowSeconds();
            p.option = config::ProfileOption{};
            p.option->update_interval = 120;
            auto subinfo = ParseSubscriptionInfo(subinfo_header);
            if (subinfo) p.extra = subinfo;

            auto& profiles = clash_yuka::AppContext::Get().profiles();
            profiles.Add(std::move(p));
            // 导入后立即激活该配置（替换 current），保证后续 ApplyConfig
            // 生成的运行时配置就是刚导入的这一份。
            profiles.current.clear();
            profiles.SetCurrent(uid);
            profiles.Save(utils::profiles_yaml_path());
            RebuildTree();
            RefreshActivateButton();
            SetStatus(wxString::FromUTF8("已导入，正在应用配置…"));
            ApplyCurrentProfileAsync();
        });
    }).detach();
}

void ProfilesPage::OnNewProfile(wxCommandEvent&) {
    // 选择类型
    wxArrayString types;
    types.Add(wxString::FromUTF8("本地配置"));
    types.Add(wxString::FromUTF8("合并配置"));
    types.Add(wxString::FromUTF8("脚本配置"));
    wxSingleChoiceDialog dlg(this, wxString::FromUTF8("选择配置类型"),
                             wxString::FromUTF8("新建配置"), types);
    if (dlg.ShowModal() != wxID_OK) return;
    int sel = dlg.GetSelection();

    config::ProfileType type;
    switch (sel) {
        case 0:  type = config::ProfileType::Local;  break;
        case 1:  type = config::ProfileType::Merge;  break;
        case 2:  type = config::ProfileType::Script; break;
        default: return;
    }

    std::string uid = config::GenerateProfileUid(type);
    std::string file_name = uid + ".yaml";
    std::optional<std::string> script_type;

    if (type == config::ProfileType::Script) {
        wxArrayString sts;
        sts.Add(wxString::FromUTF8("JavaScript"));
        sts.Add(wxString::FromUTF8("Lua"));
        wxSingleChoiceDialog st_dlg(this, wxString::FromUTF8("选择脚本类型"),
                                    wxString::FromUTF8("新建脚本配置"), sts);
        if (st_dlg.ShowModal() != wxID_OK) return;
        if (st_dlg.GetSelection() == 0) {
            script_type = "javascript";
            file_name = uid + ".js";
        } else {
            script_type = "lua";
            file_name = uid + ".lua";
        }
    }

    // 输入名称（可留空）
    wxTextEntryDialog name_dlg(this, wxString::FromUTF8("请输入名称（可留空）"),
                               wxString::FromUTF8("新建配置"));
    if (name_dlg.ShowModal() != wxID_OK) return;

    // 创建空文件
    auto file_path = utils::profiles_dir() / file_name;
    std::ofstream ofs(file_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        SetStatus(wxString::FromUTF8("无法创建文件"));
        return;
    }
    if (type == config::ProfileType::Local ||
        type == config::ProfileType::Merge) {
        ofs << "# " << config::ProfileTypeToString(type) << " profile\n";
    }
    ofs.close();

    config::Profile p;
    p.uid = uid;
    p.type = type;
    p.name = ToUtf8(name_dlg.GetValue());
    p.file = file_name;
    p.updated = NowSeconds();
    if (script_type) p.script_type = script_type;

    auto& profiles = clash_yuka::AppContext::Get().profiles();
    profiles.Add(std::move(p));
    profiles.Save(utils::profiles_yaml_path());
    RebuildTree();
    SetStatus(wxString::FromUTF8("已创建"));
}

void ProfilesPage::OnRefresh(wxCommandEvent&) {
    RebuildTree();
    RefreshActivateButton();
    SetStatus(wxString::FromUTF8("已刷新"));
}

void ProfilesPage::OnActivateCurrent(wxCommandEvent&) {
    if (selected_uid_.empty()) {
        SetStatus(wxString::FromUTF8("未选择订阅"));
        return;
    }
    auto& profiles = clash_yuka::AppContext::Get().profiles();
    profiles.SetCurrent(selected_uid_);
    SaveProfiles();
    ApplyCurrentProfileAsync();
    RefreshActivateButton();
    RebuildTree();
    SetStatus(wxString::FromUTF8("已激活，正在应用配置…"));
}

// ---- 列表事件 ----

void ProfilesPage::OnTreeSelected(wxTreeEvent& event) {
    wxTreeItemId id = event.GetItem();
    if (!id.IsOk()) return;  // 重建树时会触发无效选中，忽略
    auto* data = dynamic_cast<TreeItemData*>(tree_->GetItemData(id));
    if (data && !data->is_group && !data->uid.empty()) {
        selected_uid_ = data->uid;
        RefreshDetail();
        RefreshActivateButton();
    }
    // 分组节点：不改变当前选中
}

// ---- 详情区事件 ----

void ProfilesPage::OnSave(wxCommandEvent&) {
    if (selected_uid_.empty()) {
        SetStatus(wxString::FromUTF8("未选择订阅"));
        return;
    }
    auto& profiles = clash_yuka::AppContext::Get().profiles();
    const auto* p = profiles.Get(selected_uid_);
    if (!p) return;

    config::Profile patch;
    patch.name = ToUtf8(name_edit_->GetValue());
    if (p->type == config::ProfileType::Remote) {
        patch.url = ToUtf8(url_edit_->GetValue());
    }
    patch.updated = NowSeconds();
    patch.chain = chain_uids_;

    profiles.Update(selected_uid_, patch);
    // Profiles::Update 仅在 patch.chain 非空时更新，手动同步空链情况
    if (auto* pp = profiles.Get(selected_uid_)) {
        pp->chain = chain_uids_;
    }
    SaveProfiles();
    RebuildTree();
    SetStatus(wxString::FromUTF8("已保存"));
}

void ProfilesPage::OnDelete(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    if (wxMessageBox(wxString::FromUTF8("确定删除该订阅？"),
                     wxString::FromUTF8("确认删除"),
                     wxYES_NO | wxICON_QUESTION, this) != wxYES) {
        return;
    }

    auto& profiles = clash_yuka::AppContext::Get().profiles();
    const auto* p = profiles.Get(selected_uid_);
    std::string file_name = p ? p->file : (selected_uid_ + ".yaml");

    bool was_current = profiles.Remove(selected_uid_);
    SaveProfiles();

    // 删除 profile 文件（忽略错误）
    std::error_code ec;
    std::filesystem::remove(utils::profiles_dir() / file_name, ec);

    selected_uid_.clear();
    RebuildTree();
    RefreshDetail();
    RefreshActivateButton();
    SetStatus(wxString::FromUTF8("已删除"));

    // 删除的是当前生效配置时，立即应用新的 current（如自动激活的首个配置）
    if (was_current && !profiles.current.empty()) {
        SetStatus(wxString::FromUTF8("已删除，正在应用当前配置…"));
        ApplyCurrentProfileAsync();
    }
}

void ProfilesPage::OnDuplicate(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    auto& profiles = clash_yuka::AppContext::Get().profiles();
    const auto* p = profiles.Get(selected_uid_);
    if (!p) return;

    std::string new_uid = config::GenerateProfileUid(p->type);
    // 新文件名：new_uid + 原扩展名
    auto dot = p->file.find_last_of('.');
    std::string ext = (dot != std::string::npos) ? p->file.substr(dot) : ".yaml";
    std::string new_file = new_uid + ext;

    // 复制文件（忽略错误，即使源文件不存在也创建条目）
    std::error_code ec;
    std::filesystem::copy_file(
        utils::profiles_dir() / p->file,
        utils::profiles_dir() / new_file,
        std::filesystem::copy_options::overwrite_existing, ec);

    config::Profile np = *p;
    np.uid = new_uid;
    np.file = new_file;
    np.name = p->name + " (副本)";
    np.updated = NowSeconds();

    profiles.Add(std::move(np));
    SaveProfiles();
    RebuildTree();
    SetStatus(wxString::FromUTF8("已复制"));
}

void ProfilesPage::OnToggleActivate(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    auto& profiles = clash_yuka::AppContext::Get().profiles();
    auto it = std::find(profiles.current.begin(),
                        profiles.current.end(), selected_uid_);
    if (it != profiles.current.end()) {
        // 当前已激活 -> 停用
        profiles.current.erase(it);
        SaveProfiles();
        SetStatus(wxString::FromUTF8("已停用"));
    } else {
        // 当前未激活 -> 激活
        profiles.SetCurrent(selected_uid_);
        SaveProfiles();
        ApplyCurrentProfileAsync();
        SetStatus(wxString::FromUTF8("已激活，正在应用配置…"));
    }
    RefreshActivateButton();
    RebuildTree();
}

void ProfilesPage::OnMoveUp(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    auto& items = clash_yuka::AppContext::Get().profiles().items;
    auto it = std::find_if(items.begin(), items.end(),
        [&](const config::Profile& p) { return p.uid == selected_uid_; });
    if (it == items.begin() || it == items.end()) return;
    std::iter_swap(it, it - 1);
    SaveProfiles();
    RebuildTree();
    // 重新选中移动后的节点
    auto node = FindProfileNode(selected_uid_);
    if (node.IsOk()) tree_->SelectItem(node);
}

void ProfilesPage::OnMoveDown(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    auto& items = clash_yuka::AppContext::Get().profiles().items;
    auto it = std::find_if(items.begin(), items.end(),
        [&](const config::Profile& p) { return p.uid == selected_uid_; });
    if (it == items.end() || it + 1 == items.end()) return;
    std::iter_swap(it, it + 1);
    SaveProfiles();
    RebuildTree();
    auto node = FindProfileNode(selected_uid_);
    if (node.IsOk()) tree_->SelectItem(node);
}

// ---- 处理链编辑器事件 ----

void ProfilesPage::OnChainAdd(wxCommandEvent&) {
    if (selected_uid_.empty()) return;
    auto& profiles = clash_yuka::AppContext::Get().profiles();

    // 列出可选的 Merge/Script profile（排除自身）
    wxArrayString choices;
    std::vector<std::string> candidates;
    for (const auto& p : profiles.items) {
        if (p.uid == selected_uid_) continue;
        if (p.type != config::ProfileType::Merge &&
            p.type != config::ProfileType::Script) continue;
        choices.Add(wxString::FromUTF8(p.name + "  [" + p.uid + "]"));
        candidates.push_back(p.uid);
    }
    if (choices.IsEmpty()) {
        SetStatus(wxString::FromUTF8("没有可用的 Merge/Script 配置"));
        return;
    }
    wxSingleChoiceDialog dlg(this, wxString::FromUTF8("选择要添加的处理链项"),
                             wxString::FromUTF8("添加处理链"), choices);
    if (dlg.ShowModal() != wxID_OK) return;
    int sel = dlg.GetSelection();
    if (sel < 0 || sel >= static_cast<int>(candidates.size())) return;
    chain_uids_.push_back(candidates[sel]);
    chain_list_->Append(choices[sel]);
    SetStatus(wxString::FromUTF8("已添加到处理链（保存后生效）"));
}

void ProfilesPage::OnChainRemove(wxCommandEvent&) {
    int sel = chain_list_->GetSelection();
    if (sel == wxNOT_FOUND) {
        SetStatus(wxString::FromUTF8("未选择处理链项"));
        return;
    }
    if (sel < 0 || sel >= static_cast<int>(chain_uids_.size())) return;
    chain_uids_.erase(chain_uids_.begin() + sel);
    chain_list_->Delete(sel);
    SetStatus(wxString::FromUTF8("已从处理链移除（保存后生效）"));
}

// ---- 数据加载 / UI 重建 ----

void ProfilesPage::RebuildTree() {
    tree_->DeleteAllItems();
    auto root = tree_->AddRoot(wxEmptyString);

    for (int i = 0; i < 4; ++i) {
        config::ProfileType t;
        switch (i) {
            case 0:  t = config::ProfileType::Remote; break;
            case 1:  t = config::ProfileType::Local;  break;
            case 2:  t = config::ProfileType::Merge;  break;
            default: t = config::ProfileType::Script; break;
        }
        auto* gd = new TreeItemData();
        gd->is_group = true;
        group_roots_[i] = tree_->AppendItem(root, GroupTitle(t), -1, -1, gd);
    }

    auto& profiles = clash_yuka::AppContext::Get().profiles();
    for (const auto& p : profiles.items) {
        int idx = TypeIndex(p.type);
        wxString label = wxString::FromUTF8(p.name);
        bool active = std::find(profiles.current.begin(),
                                profiles.current.end(),
                                p.uid) != profiles.current.end();
        if (active) label += " " + wxString::FromUTF8("●");
        auto* pd = new TreeItemData();
        pd->uid = p.uid;
        pd->is_group = false;
        tree_->AppendItem(group_roots_[idx], label, -1, -1, pd);
    }

    for (int i = 0; i < 4; ++i) {
        if (group_roots_[i].IsOk()) tree_->Expand(group_roots_[i]);
    }
}

void ProfilesPage::RefreshDetail() {
    // 选中为空：清空并禁用详情区
    if (selected_uid_.empty()) {
        name_edit_->SetValue(wxEmptyString);
        url_edit_->SetValue(wxEmptyString);
        type_value_->SetLabel("--");
        updated_value_->SetLabel("--");
        subinfo_value_->SetLabel("--");
        chain_list_->Clear();
        chain_uids_.clear();
        name_edit_->Disable();
        url_edit_->Disable();
        chain_list_->Disable();
        chain_add_btn_->Disable();
        chain_remove_btn_->Disable();
        save_btn_->Disable();
        delete_btn_->Disable();
        duplicate_btn_->Disable();
        toggle_activate_btn_->Disable();
        up_btn_->Disable();
        down_btn_->Disable();
        return;
    }

    auto& profiles = clash_yuka::AppContext::Get().profiles();
    const auto* p = profiles.Get(selected_uid_);
    if (!p) {
        selected_uid_.clear();
        RefreshDetail();
        return;
    }

    name_edit_->Enable();
    name_edit_->SetValue(wxString::FromUTF8(p->name));
    type_value_->SetLabel(ProfileTypeLabel(p->type));

    if (p->type == config::ProfileType::Remote) {
        url_edit_->Enable();
        url_edit_->SetValue(p->url ? wxString::FromUTF8(*p->url) : wxString());
        if (p->extra) {
            subinfo_value_->SetLabel(wxString::Format(
                wxString::FromUTF8("↑ %s  ↓ %s / 总 %s"),
                FormatBytes(p->extra->upload),
                FormatBytes(p->extra->download),
                FormatBytes(p->extra->total)));
        } else {
            subinfo_value_->SetLabel(wxString::FromUTF8("无"));
        }
    } else {
        url_edit_->Disable();
        url_edit_->SetValue(wxEmptyString);
        subinfo_value_->SetLabel("--");
    }

    updated_value_->SetLabel(FormatTime(p->updated));

    // 处理链列表
    chain_list_->Enable();
    chain_add_btn_->Enable();
    chain_remove_btn_->Enable();
    chain_list_->Clear();
    chain_uids_.clear();
    for (const auto& cuid : p->chain) {
        const auto* cp = profiles.Get(cuid);
        wxString label = cp ? wxString::FromUTF8(cp->name)
                            : wxString::FromUTF8(cuid);
        chain_list_->Append(label);
        chain_uids_.push_back(cuid);
    }

    save_btn_->Enable();
    delete_btn_->Enable();
    duplicate_btn_->Enable();
    toggle_activate_btn_->Enable();
    up_btn_->Enable();
    down_btn_->Enable();
}

void ProfilesPage::RefreshActivateButton() {
    auto& profiles = clash_yuka::AppContext::Get().profiles();
    bool active = !selected_uid_.empty() &&
        std::find(profiles.current.begin(), profiles.current.end(),
                  selected_uid_) != profiles.current.end();
    activate_btn_->SetLabel(active
        ? wxString::FromUTF8("已激活")
        : wxString::FromUTF8("激活当前"));
    toggle_activate_btn_->SetLabel(active
        ? wxString::FromUTF8("停用")
        : wxString::FromUTF8("激活"));
}

void ProfilesPage::SetStatus(const wxString& text) {
    if (status_text_) status_text_->SetLabel(text);
}

// ---- 持久化 / 核心重启 ----

void ProfilesPage::SaveProfiles() {
    clash_yuka::AppContext::Get().profiles().Save(
        utils::profiles_yaml_path());
}

void ProfilesPage::ApplyCurrentProfileAsync() {
    auto alive = alive_;
    std::thread([this, alive] {
        if (!alive->load(std::memory_order_acquire)) return;
        // ApplyConfig 内部会：重建 config.yaml -> 热重载（失败则安全重启）-> 等 API 就绪。
        // 不再直接 process().Restart()，否则会复用旧的 config.yaml。
        std::string err = clash_yuka::AppContext::Get().ApplyConfig();
        wxTheApp->CallAfter([this, alive, err]() {
            if (!alive->load(std::memory_order_acquire)) return;
            if (err.empty()) {
                SetStatus(wxString::FromUTF8("配置已应用"));
            } else {
                SetStatus(wxString::FromUTF8("配置应用失败: ") +
                          wxString::FromUTF8(err.c_str()));
            }
        });
    }).detach();
}

// ---- 工具函数 ----

wxTreeItemId ProfilesPage::FindProfileNode(const std::string& uid) const {
    if (uid.empty()) return wxTreeItemId();
    // 遍历 4 个分组下的子节点
    for (int i = 0; i < 4; ++i) {
        if (!group_roots_[i].IsOk()) continue;
        wxTreeItemIdValue cookie;
        wxTreeItemId child = tree_->GetFirstChild(group_roots_[i], cookie);
        while (child.IsOk()) {
            auto* data = dynamic_cast<TreeItemData*>(tree_->GetItemData(child));
            if (data && !data->is_group && data->uid == uid) {
                return child;
            }
            child = tree_->GetNextChild(group_roots_[i], cookie);
        }
    }
    return wxTreeItemId();
}

wxString ProfilesPage::GroupTitle(config::ProfileType type) {
    switch (type) {
        case config::ProfileType::Remote: return wxString::FromUTF8("远程订阅");
        case config::ProfileType::Local:  return wxString::FromUTF8("本地配置");
        case config::ProfileType::Merge:  return wxString::FromUTF8("合并配置");
        case config::ProfileType::Script: return wxString::FromUTF8("脚本配置");
    }
    return "Unknown";
}

} // namespace clash_yuka::ui
