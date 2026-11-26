// ClashYuka Profiles 页面（Phase 7 Task 16）
//
// 订阅配置管理：
//   - 左侧：按类型分组的 profile 树（Remote / Local / Merge / Script）
//     使用 wxTreeCtrl 实现 4 个分组根节点，profile 作为子节点；
//     当前激活的 profile 在节点标题后追加 " ●" 标记
//   - 右侧：选中 profile 的详情编辑区（Card 内）
//     - 名称 / URL 可编辑（wxTextCtrl）
//     - 类型 / 更新时间 / 订阅信息 只读显示（只读文本）
//     - 处理链编辑器：wxListBox 列出 chain uid，[添加]/[删除] 按钮
//     - 操作按钮：[保存] [删除] [复制] [激活/停用] [上移] [下移]
//
// 数据来源与线程模型：
//   - 读取 / 编辑：直接访问 AppContext::Get().profiles()（内存数据），
//     修改后调用 Profiles::Save(<config>/profiles.yaml) 持久化
//   - 远程订阅下载：detached std::thread 中用 ix::HttpClient 拉取，
//     结果通过 CallAfter 回主线程写入文件并 Add+Save
//   - 导入/激活后立即应用配置：detached std::thread 调用 AppContext::ApplyConfig()
//     （重建 config.yaml -> 热重载或安全重启内核 -> 等待 API 就绪），
//     不再直接 process().Restart() 复用旧配置文件
//   - alive_ 标志位避免页面析构后访问悬空指针
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/config/profile.h"

namespace clash_yuka::ui {

class ProfilesPage : public wxPanel {
public:
    explicit ProfilesPage(wxWindow* parent);
    ~ProfilesPage() override;

private:
    // 树节点数据：保存 uid 与是否为分组根节点
    struct TreeItemData : public wxTreeItemData {
        std::string uid;        // profile uid（分组节点为空）
        bool is_group = false;  // true=分组根节点
    };

    // ---- 工具栏事件 ----
    void OnImportRemote(wxCommandEvent& event);
    void OnNewProfile(wxCommandEvent& event);
    void OnRefresh(wxCommandEvent& event);
    void OnActivateCurrent(wxCommandEvent& event);

    // ---- 列表事件 ----
    void OnTreeSelected(wxTreeEvent& event);

    // ---- 详情区事件 ----
    void OnSave(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnDuplicate(wxCommandEvent& event);
    void OnToggleActivate(wxCommandEvent& event);
    void OnMoveUp(wxCommandEvent& event);
    void OnMoveDown(wxCommandEvent& event);

    // ---- 处理链编辑器事件 ----
    void OnChainAdd(wxCommandEvent& event);
    void OnChainRemove(wxCommandEvent& event);

    // ---- 数据加载 / UI 重建 ----
    void RebuildTree();              // 从 profiles() 重建左侧树
    void RefreshDetail();            // 按当前选中刷新右侧详情
    void RefreshActivateButton();    // 刷新"激活当前"按钮状态
    void SetStatus(const wxString& text);

    // ---- 持久化 / 配置应用 ----
    void SaveProfiles();             // 保存 profiles.yaml
    void ApplyCurrentProfileAsync(); // detached 线程应用当前配置（重建 + 热重载/重启）

    // ---- 工具函数 ----
    wxTreeItemId FindProfileNode(const std::string& uid) const;
    static wxString GroupTitle(config::ProfileType type);

    // ---- 控件：工具栏 ----
    wxButton* import_btn_ = nullptr;
    wxButton* new_btn_ = nullptr;
    wxButton* refresh_btn_ = nullptr;
    wxButton* activate_btn_ = nullptr;

    // ---- 控件：左侧树 ----
    wxTreeCtrl* tree_ = nullptr;
    wxTreeItemId group_roots_[4];  // Remote/Local/Merge/Script 分组根

    // ---- 控件：右侧详情 ----
    wxScrolledWindow* detail_scroll_ = nullptr;
    wxPanel* detail_card_ = nullptr;
    wxTextCtrl* name_edit_ = nullptr;
    wxStaticText* type_value_ = nullptr;
    wxTextCtrl* url_edit_ = nullptr;          // Remote 才启用
    wxStaticText* updated_value_ = nullptr;
    wxStaticText* subinfo_value_ = nullptr;  // Remote 才显示
    wxListBox* chain_list_ = nullptr;
    wxButton* chain_add_btn_ = nullptr;
    wxButton* chain_remove_btn_ = nullptr;

    // 操作按钮
    wxButton* save_btn_ = nullptr;
    wxButton* delete_btn_ = nullptr;
    wxButton* duplicate_btn_ = nullptr;
    wxButton* toggle_activate_btn_ = nullptr;
    wxButton* up_btn_ = nullptr;
    wxButton* down_btn_ = nullptr;

    wxStaticText* status_text_ = nullptr;

    // ---- 当前选中状态 ----
    std::string selected_uid_;
    // 处理链 uid 列表（与 chain_list_ 显示并行，避免 client data 内存管理）
    std::vector<std::string> chain_uids_;

    // 跨线程存活标志
    std::shared_ptr<std::atomic<bool>> alive_;
};

} // namespace clash_yuka::ui
