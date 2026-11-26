// ClashYuka Connections 页面（Phase 6 Task 12）
//
// 实时连接监控页面：
//   - 虚拟化 wxListCtrl 展示连接列表（Host/Network/Type/Source/Dest/Chains/Rule/Upload/Download）
//   - 订阅 ws /connections 流，实时刷新
//   - 左侧侧栏按代理链（chains）筛选
//   - 顶部搜索框过滤 host/sourceIP 等
//   - 列头点击排序（升降序切换）
//   - 右键菜单关闭单条连接；按钮关闭全部连接
//
// 线程模型：ws 回调在后台线程 -> wxTheApp->CallAfter -> 主线程更新 UI；
// alive_ 标志位避免页面析构后访问悬空指针（与 Dashboard 一致）。
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/clash/types.h"

namespace clash_yuka::ui {

class ConnectionsPage;

// 虚拟化连接列表控件：从 ConnectionsPage 取行数据。
// wxListCtrl LC_VIRTUAL 模式要求子类实现 OnGetItemText/OnGetItemAttr，
// 仅请求可见行，可承受大量连接。
class ConnectionsListCtrl : public wxListCtrl {
public:
    ConnectionsListCtrl(wxWindow* parent, wxWindowID id, ConnectionsPage* page);

    wxString OnGetItemText(long item, long column) const override;
    wxListItemAttr* OnGetItemAttr(long item) const override;

private:
    ConnectionsPage* page_;
    // 交替行底色属性（提升大量行时的可读性）
    std::unique_ptr<wxListItemAttr> alt_attr_;
};

// Connections 页面容器：组合工具条、筛选侧栏与虚拟列表，
// 管理 ws 回调注册/注销与跨线程存活标志。
class ConnectionsPage : public wxPanel {
    friend class ConnectionsListCtrl;

public:
    explicit ConnectionsPage(wxWindow* parent);
    ~ConnectionsPage() override;

private:
    // 列定义（与 wxListCtrl 列顺序一致）
    enum Column {
        Col_Host = 0,
        Col_Network,
        Col_Type,
        Col_Source,
        Col_Dest,
        Col_Chains,
        Col_Rule,
        Col_Upload,
        Col_Download
    };

    // ---- 事件处理 ----
    void OnSearch(wxCommandEvent& event);
    void OnFilterSelected(wxCommandEvent& event);
    void OnColClick(wxListEvent& event);
    void OnItemRightClick(wxListEvent& event);
    void OnCloseConnectionMenu(wxCommandEvent& event);
    void OnCloseAll(wxCommandEvent& event);

    // ---- ws 回调注册/注销 ----
    void RegisterCallbacks();
    void UnregisterCallbacks();

    // ---- 数据处理（均在主线程）----
    void ApplyConnections(std::vector<clash::Connection> conns);
    void RebuildChainsSidebar();
    void RebuildFilteredView();
    void DoCloseConnection(const std::string& id);
    void DoCloseAllConnections();

    // ---- UI 控件 ----
    wxTextCtrl* search_box_ = nullptr;
    wxListBox* filter_list_ = nullptr;
    ConnectionsListCtrl* list_ctrl_ = nullptr;
    wxButton* close_all_btn_ = nullptr;
    wxStaticText* count_label_ = nullptr;
    wxStaticText* filter_title_ = nullptr;

    // ---- 数据 ----
    // 全量连接（最新一次 ws 推送）
    std::vector<clash::Connection> all_connections_;
    // 筛选+排序后的指针视图（供虚拟列表 OnGetItemText 读取）
    std::vector<const clash::Connection*> filtered_view_;
    // 侧栏代理链列表；索引 0 固定为 "全部"
    std::vector<std::string> chains_;
    // 上一次侧栏列表的字节签名，用于检测是否需要重建（避免每秒刷新闪烁）
    std::string chains_signature_;
    int selected_chain_idx_ = 0;

    int sort_col_ = -1;
    bool sort_asc_ = true;
    wxString search_text_;

    // 右键菜单上下文：待关闭的连接 id
    std::string context_conn_id_;

    // 跨线程存活标志：析构时置 false，CallAfter lambda 据此跳过 UI 更新。
    std::shared_ptr<std::atomic<bool>> alive_;

    // WsClient 订阅句柄（RegisterCallbacks 时获取，UnregisterCallbacks 时注销）。
    // -1 表示未订阅。
    int connections_handle_ = -1;

    wxDECLARE_EVENT_TABLE();
};

} // namespace clash_yuka::ui
