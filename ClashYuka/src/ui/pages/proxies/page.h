// ClashYuka Proxies 页面（Phase 5 Task 11）
//
// 代理页面：左侧代理组列表 + 右侧节点列表（自绘延迟 chip），
// 支持节点切换、单节点/批量延迟测试、模式切换（rule/global/direct）、
// 名称搜索与按名称/延迟排序。
//
// 数据来源与线程模型（同 Dashboard）：
//   - 所有 mihomo API 调用（GetProxySnapshot/SelectProxy/GetProxyDelay/
//     GetGroupDelay/PatchConfig/GetConfig）在 detached std::thread 中同步执行；
//   - 结果通过 wxTheApp->CallAfter marshal 回主线程更新 UI；
//   - alive_ 标志位避免页面析构后访问悬空指针（shared_ptr 延长 bool 生命周期，
//     使已投递但未执行的 CallAfter 能安全读取）。
//
// 刷新时机：内核启动、配置应用成功（导入/激活订阅）都会触发 ReloadFromApi，
// 因此无需重启 GUI 即可看到新配置的代理组与节点。
//
// 测速：
//   - 「测速」使用单节点 API  GET /proxies/{name}/delay
//   - 「全部测速」使用整组 API GET /group/{name}/delay（不会改变用户当前选择）
//   两者不是同一个接口，失败时保留 mihomo 原始错误信息并在状态栏展示。
//
// 布局：
//   - 顶部工具栏：模式切换 / 搜索 / 排序 / 测速 / 全部测速 / 刷新
//   - 主体：wxBoxSizer(HORIZONTAL)，左侧 wxListBox 固定宽度 250，右侧
//     NodeListView 自绘虚拟列表 EXPAND。
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace clash_yuka::ui {

// 节点排序方式
enum class ProxySortMode {
    Default = 0,
    ByName,
    ByDelay
};

// 节点条目（节点列表自绘用）
struct ProxyNodeEntry {
    wxString name;
    wxString type;
    int delay = -1;   // -1 未测；0 失败/超时；>0 延迟 ms
};

// 自绘节点列表：wxScrolled<wxPanel> + 虚拟行自绘。
// 每行显示：节点名 + 类型（小字）+ 右侧延迟 chip（按延迟着色）。
// 当前选中节点高亮（primary_container 背景 + 左侧 primary 强调条）。
// 线程安全：所有方法仅在主线程调用；延迟更新由 ProxiesPage 经 CallAfter
// marshal 回主线程后调用 UpdateDelay。
class NodeListView : public wxScrolled<wxPanel> {
public:
    NodeListView(wxWindow* parent, wxWindowID id = wxID_ANY);

    // 设置节点数据（覆盖）。current_name 用于高亮当前选中节点。
    void SetNodes(const std::vector<ProxyNodeEntry>& nodes,
                  const wxString& current_name);
    // 更新单个节点的延迟（按 name 匹配，原地更新不重排）。
    void UpdateDelay(const wxString& name, int delay_ms);
    // 设置当前高亮节点。
    void SetCurrent(const wxString& name);
    // 设置搜索过滤（触发重排+重绘）。
    void SetFilter(const wxString& keyword);
    // 设置排序方式（触发重排+重绘）。
    void SetSortMode(ProxySortMode mode);

    // 节点点击回调（参数：节点名）。ProxiesPage 赋值。
    std::function<void(const wxString&)> on_node_clicked;

private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnSize(wxSizeEvent& event);

    // 应用 filter + sort 重建 shown_，并刷新虚拟尺寸与画面。
    void Rebuild();
    // 按 delay 计算延迟 chip 的标签与配色。
    static void DelayColors(int delay, wxString& label,
                            wxColour& bg, wxColour& fg);
    // 绘制圆角 chip。
    static void DrawChip(wxDC& dc, int x, int y, int w, int h,
                         const wxString& label,
                         const wxColour& bg, const wxColour& fg);

    std::vector<ProxyNodeEntry> raw_;     // 原始数据（SetNodes 写入）
    std::vector<ProxyNodeEntry> shown_;   // 过滤+排序后用于绘制
    wxString current_name_;               // 当前选中节点（高亮）
    wxString filter_;
    ProxySortMode sort_mode_ = ProxySortMode::Default;

    static constexpr int kRowHeight = 44;
    static constexpr int kChipWidth = 76;
    static constexpr int kChipHeight = 24;
};

// Proxies 页面容器：组合工具栏 + 组列表 + 节点列表，管理异步 API 调用。
class ProxiesPage : public wxPanel {
public:
    explicit ProxiesPage(wxWindow* parent);
    ~ProxiesPage() override;

private:
    // 从 mihomo 拉取代理组/节点/配置（异步线程 + CallAfter）。
    void ReloadFromApi();
    // 将当前组（groups_[current_group_]）的节点列表应用到 NodeListView。
    void ApplyCurrentGroup();
    // 状态栏（失败原因 / 切换结果）
    void SetStatus(const wxString& text, bool is_error = false);

    // 事件处理
    void OnGroupSelected(wxCommandEvent& event);
    void OnNodeClicked(const wxString& node_name);
    void OnTestSelected(wxCommandEvent& event);
    void OnTestAll(wxCommandEvent& event);
    void OnSearchChanged(wxCommandEvent& event);
    void OnSortChanged(wxCommandEvent& event);
    void OnModeChanged(wxCommandEvent& event);

    // 测速参数：优先取 verge.default_latency_test，否则用默认 204 探测地址。
    std::string TestUrl() const;
    int TestTimeout() const;

    // ---- 控件 ----
    wxChoice* mode_choice_ = nullptr;
    wxTextCtrl* search_ctrl_ = nullptr;
    wxChoice* sort_choice_ = nullptr;
    wxListBox* group_list_ = nullptr;
    NodeListView* node_view_ = nullptr;
    wxButton* test_btn_ = nullptr;
    wxButton* testall_btn_ = nullptr;
    wxButton* refresh_btn_ = nullptr;
    wxStaticText* status_text_ = nullptr;

    // ---- 数据缓存（仅主线程访问）----
    struct GroupInfo {
        wxString name;
        wxString type;
        wxString now;                       // 当前选中节点
        std::vector<ProxyNodeEntry> nodes;  // 与组内节点对应的条目
    };
    std::vector<GroupInfo> groups_;
    int current_group_ = -1;
    wxString current_group_name_;           // 用于刷新后保持选中的组
    wxString current_node_name_;            // 当前高亮/待测速节点

    // 跨线程存活标志
    std::shared_ptr<std::atomic<bool>> alive_;
    // 加载代次：丢弃过期 reload 结果，避免连续导入时旧结果覆盖新结果
    std::shared_ptr<std::atomic<int>> load_gen_;

    // 内核状态订阅 handle（内核启动后自动刷新节点列表）
    int core_state_handle_ = -1;
    // 配置应用订阅 handle（导入/激活订阅后自动刷新）
    int config_applied_handle_ = -1;
};

} // namespace clash_yuka::ui
