// ClashYuka Logs 页面实现（Phase 6 Task 14）
#include "ui/pages/logs/page.h"

#include <wx/sizer.h>

#include <algorithm>
#include <cctype>
#include <thread>
#include <utility>

#include "core/app_context.h"

namespace clash_yuka::ui {

namespace {

// 将字符串转为大写（用于级别比较与搜索）
std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// 已知日志级别集合（用于筛选逻辑：未知级别始终显示）
const std::unordered_set<std::string>& KnownLevels() {
    static const std::unordered_set<std::string> set = {
        "INFO", "WARNING", "ERROR", "DEBUG"
    };
    return set;
}

// Type 列宽度
constexpr int kTypeColumnWidth = 90;

} // namespace

// ============================================================
// LogsListCtrl
// ============================================================

LogsListCtrl::LogsListCtrl(wxWindow* parent, wxWindowID id, LogsPage* page)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL),
      page_(page) {
    attr_error_.SetTextColour(wxColour(239, 68, 68));
    attr_warning_.SetTextColour(wxColour(249, 115, 22));
    attr_debug_.SetTextColour(wxColour(115, 115, 115));
    attr_default_.SetTextColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
}

wxString LogsListCtrl::OnGetItemText(long item, long column) const {
    if (!page_ || item < 0 || item >= static_cast<long>(page_->filtered_.size())) {
        return wxEmptyString;
    }
    const auto& entry = page_->filtered_[static_cast<size_t>(item)];
    if (column == 0) {
        return wxString::FromUTF8(entry.type).Upper();
    }
    return wxString::FromUTF8(entry.payload);
}

wxListItemAttr* LogsListCtrl::OnGetItemAttr(long item) const {
    if (!page_ || item < 0 || item >= static_cast<long>(page_->filtered_.size())) {
        return nullptr;
    }
    const std::string& type = page_->filtered_[static_cast<size_t>(item)].type;
    // mihomo 的 type 可能为小写（info/warning/error/debug），两种都匹配
    if (type == "error" || type == "ERROR") return &attr_error_;
    if (type == "warning" || type == "WARNING") return &attr_warning_;
    if (type == "debug" || type == "DEBUG") return &attr_debug_;
    return &attr_default_;
}

// ============================================================
// LogsPage
// ============================================================

LogsPage::LogsPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    enabled_levels_ = {"INFO", "WARNING", "ERROR", "DEBUG"};

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // 状态提示（内核未运行时显示）
    status_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    status_text_->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    status_text_->SetForegroundColour(wxColour(255, 149, 0));
    outer->Add(status_text_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* row1 = new wxBoxSizer(wxHORIZONTAL);

    search_input_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    search_input_->SetHint(wxString::FromUTF8("搜索日志..."));
    search_input_->Bind(wxEVT_TEXT, &LogsPage::OnSearchText, this);
    row1->Add(search_input_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    clear_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("清空"));
    clear_btn_->Bind(wxEVT_BUTTON, &LogsPage::OnClear, this);
    row1->Add(clear_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    autoscroll_btn_ = new wxToggleButton(this, wxID_ANY, wxString::FromUTF8("自动滚动"));
    autoscroll_btn_->SetValue(true);
    autoscroll_btn_->Bind(wxEVT_TOGGLEBUTTON, &LogsPage::OnAutoScrollToggle, this);
    row1->Add(autoscroll_btn_, 0, wxALIGN_CENTER_VERTICAL);

    outer->Add(row1, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* row2 = new wxBoxSizer(wxHORIZONTAL);

    const std::vector<std::string> levels = {"INFO", "WARNING", "ERROR", "DEBUG"};
    for (const auto& lvl : levels) {
        auto* checkbox = new wxCheckBox(this, wxID_ANY, wxString::FromUTF8(lvl.c_str()));
        checkbox->SetValue(true);
        std::string level = lvl;
        checkbox->Bind(wxEVT_CHECKBOX, [this, level](wxCommandEvent& event) {
            OnLevelChip(level, event.GetInt() != 0);
        });
        level_checkboxes_.push_back(checkbox);
        row2->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    }

    outer->Add(row2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    list_ = new LogsListCtrl(this, wxID_ANY, this);
    list_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    list_->AppendColumn(wxString::FromUTF8("Type"), wxLIST_FORMAT_LEFT,
                        kTypeColumnWidth);
    list_->AppendColumn(wxString::FromUTF8("Payload"), wxLIST_FORMAT_LEFT, 600);
    list_->SetItemCount(0);
    list_->Bind(wxEVT_SIZE, &LogsPage::OnListResize, this);

    outer->Add(list_, 1, wxEXPAND | wxALL, 8);
    SetSizer(outer);
    Layout();

    if (clash_yuka::AppContext::Get().IsInitialized()) {
        RegisterCallbacks();
        // 初始化状态提示
        if (!clash_yuka::AppContext::Get().IsCoreRunning()) {
            const auto& err = clash_yuka::AppContext::Get().last_start_error();
            status_text_->SetLabel(
                wxString::FromUTF8(err.empty() ? "内核未运行，日志不会更新" : err));
        }
    }
}

LogsPage::~LogsPage() {
    UnregisterCallbacks();
}

void LogsPage::RegisterCallbacks() {
    auto& ctx = clash_yuka::AppContext::Get();
    auto alive = alive_;

    // ws /logs 回调：LogEntry {type, payload}
    log_handle_ = ctx.ws().SubscribeLog(
        [this, alive](const clash::LogEntry& entry) {
            // 拷贝一份供 CallAfter 捕获（ws 后台线程可能复用 entry 引用）
            clash::LogEntry copy = entry;
            wxTheApp->CallAfter([this, alive, copy]() {
                if (!alive->load(std::memory_order_acquire)) return;
                AppendLog(copy);
            });
        });
}

void LogsPage::UnregisterCallbacks() {
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        if (log_handle_ != -1) {
            clash_yuka::AppContext::Get().ws().UnsubscribeLog(log_handle_);
            log_handle_ = -1;
        }
    }
}

// ---- 事件处理 ----

void LogsPage::OnSearchText(wxCommandEvent&) {
    search_upper_ = search_input_->GetValue().Upper();
    RebuildFiltered();
}

void LogsPage::OnClear(wxCommandEvent&) {
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_.clear();
    }
    filtered_.clear();
    list_->SetItemCount(0);
}

void LogsPage::OnLevelChip(const std::string& level, bool enabled) {
    if (enabled) {
        enabled_levels_.insert(level);
    } else {
        enabled_levels_.erase(level);
    }
    RebuildFiltered();
}

void LogsPage::OnAutoScrollToggle(wxCommandEvent&) {
    autoscroll_ = autoscroll_btn_->GetValue();
    if (autoscroll_ && !filtered_.empty()) {
        list_->EnsureVisible(static_cast<long>(filtered_.size()) - 1);
    }
}

void LogsPage::OnListResize(wxSizeEvent& event) {
    if (list_) {
        int total = list_->GetClientSize().x;
        // Type 列固定宽度，Payload 列填满剩余空间
        list_->SetColumnWidth(0, kTypeColumnWidth);
        list_->SetColumnWidth(1, std::max(200, total - kTypeColumnWidth - 2));
    }
    event.Skip();
}

// ---- 日志处理 ----

void LogsPage::AppendLog(const clash::LogEntry& entry) {
    // 收到第一条日志时清除状态提示
    if (status_text_ && !status_text_->GetLabel().empty()) {
        status_text_->SetLabel(wxEmptyString);
    }

    // 追加到全量缓冲（mutex 保护）
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_.push_back(entry);
        while (buffer_.size() > kMaxLogs) {
            buffer_.pop_front();
        }
    }

    // 若匹配当前筛选条件，追加到显示视图
    if (MatchesFilter(entry)) {
        filtered_.push_back(entry);
        while (filtered_.size() > kMaxLogs) {
            filtered_.pop_front();
        }
        const long count = static_cast<long>(filtered_.size());
        list_->SetItemCount(count);
        if (autoscroll_ && count > 0) {
            list_->EnsureVisible(count - 1);
        }
    }
}

void LogsPage::RebuildFiltered() {
    // 从 buffer_ 拷贝一份（持锁时间最短）
    std::deque<clash::LogEntry> buf;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buf = buffer_;
    }

    filtered_.clear();
    for (const auto& e : buf) {
        if (MatchesFilter(e)) {
            filtered_.push_back(e);
        }
    }
    while (filtered_.size() > kMaxLogs) {
        filtered_.pop_front();
    }

    const long count = static_cast<long>(filtered_.size());
    list_->SetItemCount(count);
    if (autoscroll_ && count > 0) {
        list_->EnsureVisible(count - 1);
    }
}

bool LogsPage::MatchesFilter(const clash::LogEntry& entry) const {
    // 级别筛选：已知级别受 Chip 控制，未知级别始终显示
    const std::string upper = ToUpper(entry.type);
    if (KnownLevels().count(upper)) {
        if (!enabled_levels_.count(upper)) return false;
    }

    // 搜索过滤：对 payload 做大小写无关子串匹配
    if (!search_upper_.IsEmpty()) {
        const wxString payload = wxString::FromUTF8(entry.payload).Upper();
        if (!payload.Contains(search_upper_)) return false;
    }

    return true;
}

} // namespace clash_yuka::ui
