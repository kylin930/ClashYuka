// ClashYuka Settings 页面（Phase 8 Task 19）
//
// 应用设置界面：左侧导航 + 右侧滚动内容区（6 个子页）。
//   - System       系统代理 / 代理守卫 / 自启 / 静默启动 / bypass
//   - UI           主题模式 / 主题色 / 语言 / 窗口置顶
//   - Clash        内核管理 / 端口 / 随机端口 / TUN / 日志级别 / 字段过滤 /
//                  默认延迟测试 URL / 内存与性能（GOGC、GOMEMLIMIT、进程匹配、Sniffer）
//   - Hotkeys      热键
//   - Debug        配置目录
//   - About        版本 / 构建信息 / 致谢
//
// 保存机制：每个设置项变更时立即更新 AppContext::Get().verge()，
// 调用 IVerge::Save(<config>/verge.yaml) 持久化。需要核心重启的变更
// （端口、TUN 等）额外 detach 一个线程调用 AppContext::ApplyConfig()
// （重建 config.yaml 后热重载或安全重启内核）。
//
// 布局：
//   ┌──────────────┬─────────────────────────────────────┐
//   │ 设置导航     │  内容区（wxScrolledWindow）         │
//   │  System      │  ┌───────────────────────────────┐ │
//   │  UI          │  │ SettingCard                   │ │
//   │  Clash       │  │  [选项名]  [开关/输入框]      │ │
//   │  Controller  │  └───────────────────────────────┘ │
//   │  Nyanpasu    │  ┌───────────────────────────────┐ │
//   │  Debug       │  │ SettingCard                   │ │
//   │  About       │  │  ...                           │ │
//   │              │  └───────────────────────────────┘ │
//   └──────────────┴─────────────────────────────────────┘
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace clash_yuka::ui {

// 设置卡片：左侧"标题 + 描述"，右侧"控件区"的通用行封装。
// 通过 AddControl() 在右侧添加开关/下拉/输入框等控件，减少重复代码。
class SettingCard : public wxPanel {
public:
    // 构造一个 SettingCard。
    // title: 选项名（粗体）；description: 二级说明（可空，灰色小字）
    SettingCard(wxWindow* parent, const wxString& title,
                const wxString& description = wxEmptyString);

    // 在右侧控件区添加一个控件（右对齐，垂直方向由 stretch spacer 居中）。
    void AddControl(wxWindow* ctrl, int flags = wxALIGN_RIGHT,
                    int border = 0);

private:
    wxBoxSizer* right_sizer_ = nullptr;
};

// Settings 页面：左侧导航 + 右侧滚动内容（7 个子页）
class SettingsPage : public wxPanel {
public:
    explicit SettingsPage(wxWindow* parent);
    ~SettingsPage() override = default;

private:
    // 侧栏导航项索引（与 nav_list_ / book_ 顺序一致）
    enum class SubPage : int {
        System = 0,
        UI,
        Clash,
        Hotkeys,
        Debug,
        About
    };

    // ---- 子页构建 ----
    void BuildSystemPage(wxScrolledWindow* parent);
    void BuildUIPage(wxScrolledWindow* parent);
    void BuildClashPage(wxScrolledWindow* parent);
    void BuildHotkeysPage(wxScrolledWindow* parent);
    void BuildDebugPage(wxScrolledWindow* parent);
    void BuildAboutPage(wxScrolledWindow* parent);

    // ---- 内核管理区域（Clash 子页顶部）----
    void BuildCoreManagementSection(wxScrolledWindow* parent, wxSizer* sizer);

    // ---- 事件 ----
    void OnNavSelected(wxCommandEvent& event);
    void OnCoreChoice(wxCommandEvent& event);
    void OnCheckCoreUpdate(wxCommandEvent& event);
    void OnDownloadCore(wxCommandEvent& event);

    // ---- 配置保存辅助 ----
    // 将 AppContext::Get().verge() 当前状态持久化到 <config>/verge.yaml。
    // AppContext 未初始化时静默跳过。
    void SaveVerge();
    // 持久化后额外触发核心重启（用于端口/TUN 等需重启核心的变更）。
    void SaveVergeAndRestartCore();
    // 持久化后强制重启内核（用于 GOGC/GOMEMLIMIT 等只在新进程生效的变更）。
    void SaveVergeAndForceRestartCore();
    // GOGC/GOMEMLIMIT 输入防抖：停止调节 800ms 后只重启一次
    void OnMemoryApplyTimer(wxTimerEvent& event);

    // ---- 控件 ----
    wxListBox* nav_list_ = nullptr;
    wxSimplebook* book_ = nullptr;
    wxTimer memory_apply_timer_;
    bool memory_apply_pending_ = false;

    // ---- 内核管理控件 ----
    wxChoice* core_choice_ = nullptr;
    wxStaticText* core_version_text_ = nullptr;
    wxStaticText* core_latest_text_ = nullptr;
    wxButton* core_download_btn_ = nullptr;
    wxGauge* core_download_progress_ = nullptr;
    wxStaticText* core_status_text_ = nullptr;

    // ---- GeoIP/GeoSite 资源管理控件 ----
    wxStaticText* geoip_status_text_ = nullptr;
    wxStaticText* geosite_status_text_ = nullptr;
    wxStaticText* geoip_latest_text_ = nullptr;
    wxButton* geoip_download_btn_ = nullptr;
    wxButton* geosite_download_btn_ = nullptr;
    wxGauge* geoip_download_progress_ = nullptr;
    wxGauge* geosite_download_progress_ = nullptr;
    wxStaticText* geoip_action_status_ = nullptr;

    // ---- 事件 ----
    void OnCheckGeoUpdate(wxCommandEvent& event);
    void OnDownloadGeoIP(wxCommandEvent& event);
    void OnDownloadGeoSite(wxCommandEvent& event);

    // ---- 资源状态查询 ----
    // 查询本地 GeoIP/GeoSite 文件状态并更新 UI 文本
    void UpdateGeoAssetStatus();
};

} // namespace clash_yuka::ui
