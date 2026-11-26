// ClashYuka Logs 页面（Phase 6 Task 14）
//
// 实时日志查看器：
//   - wxListCtrl 虚拟模式（wxLC_VIRTUAL）显示日志，避免大量条目时的性能问题
//   - 列：Type（级别）、Payload（内容）
//   - 级别筛选：Filter Chip 切换 INFO/WARNING/ERROR/DEBUG
//   - 搜索框：TextInput，对 payload 做大小写无关子串过滤
//   - 清空按钮：清空当前日志缓冲
//   - 自动滚动：新日志到达时滚动到底部（可暂停）
//
// 数据来源与线程模型：
//   - ws /logs 回调（WsClient 后台线程）-> wxTheApp->CallAfter -> UI 主线程追加
//   - 日志缓冲 buffer_ 由 mutex 保护；filtered_ 仅 UI 线程访问
//   - alive_ 标志位避免页面析构后访问悬空指针
//
// 缓冲策略：
//   - buffer_（全部日志）与 filtered_（筛选后视图）均上限 5000 条，超出丢弃最早条目
//   - 过滤条件变化时从 buffer_ 重建 filtered_
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/clash/types.h"

namespace clash_yuka::ui {

class LogsPage;

// 虚拟化日志列表控件：从 LogsPage 取行数据。
// wxListCtrl LC_VIRTUAL 模式仅请求可见行，可承受大量日志条目。
// 颜色属性由本控件持有（构造时按当前主题初始化）。
class LogsListCtrl : public wxListCtrl {
public:
    LogsListCtrl(wxWindow* parent, wxWindowID id, LogsPage* page);

    wxString OnGetItemText(long item, long column) const override;
    wxListItemAttr* OnGetItemAttr(long item) const override;

private:
    LogsPage* page_;
    // 级别颜色属性（按主题初始化）
    mutable wxListItemAttr attr_error_;
    mutable wxListItemAttr attr_warning_;
    mutable wxListItemAttr attr_debug_;
    mutable wxListItemAttr attr_default_;
};

class LogsPage : public wxPanel {
    friend class LogsListCtrl;

public:
    explicit LogsPage(wxWindow* parent);
    ~LogsPage() override;

private:
    // ---- ws 回调注册 ----
    void RegisterCallbacks();
    void UnregisterCallbacks();

    // ---- 事件处理 ----
    void OnSearchText(wxCommandEvent& event);
    void OnClear(wxCommandEvent& event);
    void OnLevelChip(const std::string& level, bool enabled);
    void OnAutoScrollToggle(wxCommandEvent& event);
    void OnListResize(wxSizeEvent& event);

    // ---- 日志处理（均在主线程调用）----
    void AppendLog(const clash::LogEntry& entry);
    void RebuildFiltered();
    bool MatchesFilter(const clash::LogEntry& entry) const;

    // ---- 控件 ----
    wxTextCtrl* search_input_ = nullptr;
    wxButton* clear_btn_ = nullptr;
    wxToggleButton* autoscroll_btn_ = nullptr;
    std::vector<wxCheckBox*> level_checkboxes_;
    LogsListCtrl* list_ = nullptr;
    wxStaticText* status_text_ = nullptr;

    // ---- 日志缓冲 ----
    // buffer_：全部日志（mutex 保护），上限 kMaxLogs
    std::mutex buffer_mutex_;
    std::deque<clash::LogEntry> buffer_;
    static constexpr size_t kMaxLogs = 5000;

    // filtered_：筛选后的显示视图（仅 UI 线程访问），上限 kMaxLogs
    std::deque<clash::LogEntry> filtered_;

    // ---- 过滤状态 ----
    wxString search_upper_;  // 大写化的搜索文本（避免每条日志重复转换）
    std::unordered_set<std::string> enabled_levels_;  // 已启用的级别（大写）

    bool autoscroll_ = true;

    // 跨线程存活标志
    std::shared_ptr<std::atomic<bool>> alive_;

    // WsClient 订阅句柄（RegisterCallbacks 时获取，UnregisterCallbacks 时注销）。
    // -1 表示未订阅。
    int log_handle_ = -1;
};

} // namespace clash_yuka::ui
