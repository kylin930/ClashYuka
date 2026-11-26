// ClashYuka Settings 页面实现（Phase 8 Task 19）
#include "ui/pages/settings/page.h"

#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>  // wxEVT_TOGGLEBUTTON

#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <wx/font.h>
#include <wx/settings.h>

#include "core/app_context.h"
#include "core/autostart.h"
#include "core/clash/core_manager.h"
#include "core/clash/downloader.h"
#include "core/config/verge_config.h"
#include "core/hotkey.h"
#include "core/resources.h"
#include "core/sysopt.h"
#include "core/utils/dirs.h"
#include "ui/theme.h"

namespace clash_yuka::ui {

namespace {

// 应用版本（硬编码）
constexpr const char* kAppVersion = "ClashYuka 0.1.0";

// wxString -> std::string（UTF-8）。ToUTF8() 返回的 scoped buffer 在表达式内有效，
// 立即构造 std::string 拷贝字符。
std::string ToUtf8(const wxString& s) {
    const char* p = s.ToUTF8().data();
    return p ? std::string(p) : std::string();
}

// 从多行文本拆出非空行（用于 hotkeys 编辑）
std::vector<std::string> SplitLines(const wxString& text) {
    std::vector<std::string> out;
    wxArrayString lines = wxSplit(text, '\n', '\0');
    for (size_t i = 0; i < lines.size(); ++i) {
        wxString trimmed = lines[i];
        trimmed.Trim(false).Trim(true);  // 先左后右去空白
        if (!trimmed.empty()) {
            out.push_back(ToUtf8(trimmed));
        }
    }
    return out;
}

// 把字符串列表合并为多行文本
wxString JoinLines(const std::vector<std::string>& lines) {
    wxString out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += "\n";
        out += wxString::FromUTF8(lines[i]);
    }
    return out;
}

// 从 C 风格字符串列表构造 wxArrayString（用于 wxChoice 选项）
wxArrayString MakeChoices(std::initializer_list<const char*> items) {
    wxArrayString arr;
    for (const auto* s : items) {
        arr.Add(wxString::FromUTF8(s));
    }
    return arr;
}

// 在 choices 中查找 value 的索引，找不到返回 -1
int FindChoiceIndex(const wxArrayString& choices, const std::string& value) {
    const wxString target = wxString::FromUTF8(value);
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].IsSameAs(target)) return static_cast<int>(i);
    }
    return -1;
}

// 编译器信息（用于 About 页）
wxString CompilerInfo() {
#if defined(_MSC_VER)
    return wxString::Format("MSVC %d", _MSC_VER);
#elif defined(__clang__)
    return wxString::Format("Clang %d.%d", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    return wxString::Format("GCC %d.%d", __GNUC__, __GNUC_MINOR__);
#else
    return wxString::FromUTF8("Unknown");
#endif
}

// ---- 卡片构建辅助 ----

// 子页顶部大标题
void AddPageHeader(wxWindow* parent, wxSizer* sizer, const wxString& title) {
    auto* text = new wxStaticText(parent, wxID_ANY, title);
    text->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(text, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 24);
    sizer->AddSpacer(4);
}

// 子页内小节标题
void AddSubHeader(wxWindow* parent, wxSizer* sizer, const wxString& title) {
    auto* text = new wxStaticText(parent, wxID_ANY, title);
    text->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(text, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 16);
}

// 普通文本块
void AddTextBlock(wxWindow* parent, wxSizer* sizer, const wxString& text,
                  int size = 11) {
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(wxFont(size, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(t, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP, 8);
}

// 描述性文本块（灰色小字）
void AddDescBlock(wxWindow* parent, wxSizer* sizer, const wxString& text) {
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    sizer->Add(t, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP, 4);
}

// 开关卡片：标题 + 描述 + ToggleButton，变更时调用 on_change(bool)
void AddSwitchCard(wxWindow* parent, wxSizer* sizer,
                   const wxString& title, const wxString& desc,
                   bool initial,
                   std::function<void(bool)> on_change) {
    auto* card = new SettingCard(parent, title, desc);
    auto* btn = new wxToggleButton(card, wxID_ANY, wxEmptyString);
    btn->SetValue(initial);
    btn->Bind(wxEVT_TOGGLEBUTTON, [on_change](wxCommandEvent& e) {
        on_change(e.GetInt() != 0);
    });
    card->AddControl(btn);
    sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
}

// 下拉选择卡片：标题 + 描述 + Choice，变更时调用 on_change(string)
// choices 为显示字符串列表，存储值与显示值相同。
void AddChoiceCard(wxWindow* parent, wxSizer* sizer,
                   const wxString& title, const wxString& desc,
                   const wxArrayString& choices, int initial_index,
                   std::function<void(const std::string&)> on_change) {
    auto* card = new SettingCard(parent, title, desc);
    auto* choice = new wxChoice(card, wxID_ANY, wxDefaultPosition,
                                wxSize(200, -1), choices);
    if (initial_index >= 0 && initial_index < static_cast<int>(choices.size())) {
        choice->SetSelection(initial_index);
    }
    choice->Bind(wxEVT_CHOICE, [choice, on_change](wxCommandEvent&) {
        int sel = choice->GetSelection();
        if (sel >= 0) {
            on_change(ToUtf8(choice->GetString(sel)));
        }
    });
    card->AddControl(choice);
    sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
}

// 数字输入卡片：标题 + 描述 + SpinCtrl，变更时调用 on_change(int)
void AddSpinCard(wxWindow* parent, wxSizer* sizer,
                 const wxString& title, const wxString& desc,
                 int min, int max, int initial,
                 std::function<void(int)> on_change) {
    auto* card = new SettingCard(parent, title, desc);
    auto* spin = new wxSpinCtrl(card, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxSize(120, -1),
                                wxSP_ARROW_KEYS, min, max, initial);
    spin->Bind(wxEVT_SPINCTRL, [spin, on_change](wxCommandEvent&) {
        on_change(spin->GetValue());
    });
    card->AddControl(spin);
    sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
}

// 文本输入卡片：标题 + 描述 + TextCtrl，失焦或回车时调用 on_change(string)
// multiline=true 时为多行文本（CSS / DNS / 列表编辑等）
void AddTextCard(wxWindow* parent, wxSizer* sizer,
                 const wxString& title, const wxString& desc,
                 const wxString& initial, bool multiline, int width,
                 std::function<void(const wxString&)> on_change) {
    auto* card = new SettingCard(parent, title, desc);
    long style = multiline ? wxTE_MULTILINE : wxTE_PROCESS_ENTER;
    int height = multiline ? 80 : -1;
    auto* text = new wxTextCtrl(card, wxID_ANY, initial,
                                wxDefaultPosition, wxSize(width, height),
                                style);
    // 失焦时保存（多行文本无回车保存）
    text->Bind(wxEVT_KILL_FOCUS, [text, on_change](wxFocusEvent&) {
        on_change(text->GetValue());
    });
    // 单行文本回车也保存
    if (!multiline) {
        text->Bind(wxEVT_TEXT_ENTER, [text, on_change](wxCommandEvent&) {
            on_change(text->GetValue());
        });
    }
    card->AddControl(text);
    sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
}

} // namespace

// ============================================================
// SettingCard
// ============================================================

SettingCard::SettingCard(wxWindow* parent, const wxString& title,
                         const wxString& description)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSIMPLE_BORDER) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

    auto* outer = new wxBoxSizer(wxHORIZONTAL);

    // 左侧：标题 + 描述（垂直堆叠）
    auto* text_sizer = new wxBoxSizer(wxVERTICAL);
    auto* title_text = new wxStaticText(this, wxID_ANY, title);
    title_text->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    title_text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    text_sizer->Add(title_text, 0, wxALIGN_LEFT | wxTOP, 12);

    if (!description.empty()) {
        auto* desc_text = new wxStaticText(this, wxID_ANY, description);
        desc_text->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        desc_text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        text_sizer->Add(desc_text, 0, wxALIGN_LEFT | wxTOP, 4);
    }
    text_sizer->Add(0, 0, 0, wxBOTTOM, 12);

    outer->Add(text_sizer, 1, wxEXPAND | wxLEFT, 16);

    // 右侧：控件区（垂直居中）
    right_sizer_ = new wxBoxSizer(wxVERTICAL);
    right_sizer_->AddStretchSpacer(1);
    outer->Add(right_sizer_, 0, wxEXPAND | wxALL, 12);

    SetSizer(outer);
    SetMinSize(wxSize(-1, 56));
}

void SettingCard::AddControl(wxWindow* ctrl, int flags, int border) {
    right_sizer_->Add(ctrl, 0, flags | wxRIGHT, border);
    Layout();
}

// ============================================================
// SettingsPage
// ============================================================

SettingsPage::SettingsPage(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxHORIZONTAL);

    // ---- 左侧导航 ----
    nav_list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition,
                              wxSize(170, -1));
    nav_list_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
    nav_list_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    nav_list_->Append(wxString::FromUTF8("System"));
    nav_list_->Append(wxString::FromUTF8("User Interface"));
    nav_list_->Append(wxString::FromUTF8("Clash"));
    nav_list_->Append(wxString::FromUTF8("快捷键"));
    nav_list_->Append(wxString::FromUTF8("Debug"));
    nav_list_->Append(wxString::FromUTF8("About"));
    nav_list_->SetSelection(0);
    nav_list_->Bind(wxEVT_LISTBOX, &SettingsPage::OnNavSelected, this);
    outer->Add(nav_list_, 0, wxEXPAND);

    // ---- 右侧 wxSimplebook：每页一个 wxScrolledWindow ----
    book_ = new wxSimplebook(this);
    book_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto make_page = [this]() -> wxScrolledWindow* {
        auto* page = new wxScrolledWindow(book_, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
        page->SetScrollRate(5, 5);
        page->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        return page;
    };

    {
        auto* p = make_page();
        BuildSystemPage(p);
        book_->AddPage(p, "System");
    }
    {
        auto* p = make_page();
        BuildUIPage(p);
        book_->AddPage(p, "UI");
    }
    {
        auto* p = make_page();
        BuildClashPage(p);
        book_->AddPage(p, "Clash");
    }
    {
        auto* p = make_page();
        BuildHotkeysPage(p);
        book_->AddPage(p, "快捷键");
    }
    {
        auto* p = make_page();
        BuildDebugPage(p);
        book_->AddPage(p, "Debug");
    }
    {
        auto* p = make_page();
        BuildAboutPage(p);
        book_->AddPage(p, "About");
    }
    book_->SetSelection(0);

    // GOGC/GOMEMLIMIT 调节防抖：连续点箭头时只在停止后重启一次
    memory_apply_timer_.SetOwner(this, wxID_HIGHEST + 2);
    Bind(wxEVT_TIMER, &SettingsPage::OnMemoryApplyTimer, this,
         wxID_HIGHEST + 2);

    outer->Add(book_, 1, wxEXPAND);
    SetSizer(outer);
    Layout();
}

void SettingsPage::OnNavSelected(wxCommandEvent& event) {
    int sel = event.GetInt();
    if (sel >= 0 && sel < static_cast<int>(book_->GetPageCount())) {
        book_->SetSelection(sel);
    }
}

void SettingsPage::SaveVerge() {
    auto& ctx = AppContext::Get();
    if (!ctx.IsInitialized()) return;
    ctx.verge().Save(utils::verge_config_path());
}

void SettingsPage::SaveVergeAndRestartCore() {
    SaveVerge();
    std::thread([] {
        if (!AppContext::Get().IsInitialized()) return;
        // 通过 ApplyConfig 重建 config.yaml 后再应用：
        // 端口/TUN 等监听类字段变更走安全重启，其余变更走热重载；
        // 直接 process().Restart() 会复用旧 config.yaml，导致设置不生效。
        std::string err = AppContext::Get().ApplyConfig();
        if (!err.empty()) {
            std::cerr << "[Settings] 应用配置失败: " << err << std::endl;
            const std::string detail = err;
            wxTheApp->CallAfter([detail]() {
                wxMessageBox(wxString::FromUTF8("应用配置失败：\n") +
                                 wxString::FromUTF8(detail.c_str()),
                             wxString::FromUTF8("设置"),
                             wxOK | wxICON_ERROR);
            });
        }
    }).detach();
}

void SettingsPage::SaveVergeAndForceRestartCore() {
    SaveVerge();
    std::thread([] {
        if (!AppContext::Get().IsInitialized()) return;
        std::string err = AppContext::Get().RestartCore();
        if (err.empty() && !AppContext::Get().WaitForApiReady(20000)) {
            err = "内核已重启但 API 未就绪";
        }
        if (!err.empty()) {
            std::cerr << "[Settings] 重启内核失败: " << err << std::endl;
            const std::string detail = err;
            wxTheApp->CallAfter([detail]() {
                wxMessageBox(wxString::FromUTF8("重启内核失败：\n") +
                                 wxString::FromUTF8(detail.c_str()),
                             wxString::FromUTF8("设置"),
                             wxOK | wxICON_ERROR);
            });
        }
    }).detach();
}

void SettingsPage::OnMemoryApplyTimer(wxTimerEvent&) {
    if (!memory_apply_pending_) return;
    memory_apply_pending_ = false;
    SaveVergeAndForceRestartCore();
}

// ============================================================
// SubTask 19.2: System 子页
// ============================================================

void SettingsPage::BuildSystemPage(wxScrolledWindow* parent) {
    auto& verge = AppContext::Get().verge();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("系统"));

    // 系统代理开关
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("系统代理"),
                  wxString::FromUTF8("启用后将设置系统代理为 mihomo mixed-port"),
                  verge.enable_system_proxy.value_or(false),
                  [this](bool v) {
                      auto& ctx = AppContext::Get();
                      ctx.verge().enable_system_proxy = v;
                      SaveVerge();
                      // 实际启用/禁用系统代理
                      if (v) {
                          uint16_t port = ctx.GetMixedPort();
                          std::string bypass = ctx.verge().system_proxy_bypass.value_or("");
                          core::SysProxy::Get().Enable(port, bypass);
                          // 若同时启用代理守卫，启动守护线程
                          if (ctx.verge().enable_proxy_guard.value_or(false)) {
                              uint32_t interval =
                                  static_cast<uint32_t>(ctx.verge().proxy_guard_interval.value_or(10));
                              core::SysProxy::Get().StartGuard(port, bypass, interval);
                          }
                      } else {
                          core::SysProxy::Get().StopGuard();
                          core::SysProxy::Get().Disable();
                      }
                  });

    // 代理守卫开关
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("代理守卫"),
                  wxString::FromUTF8("周期性检查并修复系统代理设置"),
                  verge.enable_proxy_guard.value_or(false),
                  [this](bool v) {
                      auto& ctx = AppContext::Get();
                      ctx.verge().enable_proxy_guard = v;
                      SaveVerge();
                      // 仅在系统代理已启用时调整守护线程
                      if (ctx.verge().enable_system_proxy.value_or(false)) {
                          if (v) {
                              uint16_t port = ctx.GetMixedPort();
                              std::string bypass = ctx.verge().system_proxy_bypass.value_or("");
                              uint32_t interval =
                                  static_cast<uint32_t>(ctx.verge().proxy_guard_interval.value_or(10));
                              core::SysProxy::Get().StartGuard(port, bypass, interval);
                          } else {
                              core::SysProxy::Get().StopGuard();
                          }
                      }
                  });

    // 代理守卫间隔
    AddSpinCard(parent, sizer,
                wxString::FromUTF8("代理守卫间隔"),
                wxString::FromUTF8("检查间隔（秒）"),
                1, 3600,
                static_cast<int>(verge.proxy_guard_interval.value_or(30)),
                [this](int v) {
                    auto& ctx = AppContext::Get();
                    ctx.verge().proxy_guard_interval = static_cast<uint64_t>(v);
                    SaveVerge();
                    // 若系统代理与代理守卫均已启用，重启守护线程以应用新间隔
                    if (ctx.verge().enable_system_proxy.value_or(false) &&
                        ctx.verge().enable_proxy_guard.value_or(false)) {
                        uint16_t port = ctx.GetMixedPort();
                        std::string bypass = ctx.verge().system_proxy_bypass.value_or("");
                        core::SysProxy::Get().StopGuard();
                        core::SysProxy::Get().StartGuard(port, bypass, static_cast<uint32_t>(v));
                    }
                });

    // 服务模式：功能尚未在此页面提供，不再展示禁用占位项

    // 开机自启：读取注册表真实状态（可能用户手动改过）
    {
        bool auto_launch_enabled = core::AutoStart::Get().IsEnabled();
        // 同步 verge 配置（以注册表真实状态为准）
        AppContext::Get().verge().enable_auto_launch = auto_launch_enabled;
        AddSwitchCard(parent, sizer,
                      wxString::FromUTF8("开机自启"),
                      wxString::FromUTF8("系统登录时自动启动应用"),
                      auto_launch_enabled,
                      [this](bool v) {
                          auto& ctx = AppContext::Get();
                          ctx.verge().enable_auto_launch = v;
                          SaveVerge();
                          if (v) {
                              core::AutoStart::Get().Enable();
                          } else {
                              core::AutoStart::Get().Disable();
                          }
                      });
    }

    // 静默启动
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("静默启动"),
                  wxString::FromUTF8("启动时最小化到托盘，不显示主窗口"),
                  verge.enable_silent_start.value_or(false),
                  [this](bool v) {
                      AppContext::Get().verge().enable_silent_start = v;
                      SaveVerge();
                  });

    // 系统代理 bypass
    AddTextCard(parent, sizer,
                wxString::FromUTF8("系统代理 bypass"),
                wxString::FromUTF8("不经过代理的地址列表（逗号分隔）"),
                wxString::FromUTF8(verge.system_proxy_bypass.value_or("")),
                false, 280,
                [this](const wxString& v) {
                    auto& ctx = AppContext::Get();
                    ctx.verge().system_proxy_bypass = ToUtf8(v);
                    SaveVerge();
                    // 若系统代理已启用，实时更新 bypass
                    if (ctx.verge().enable_system_proxy.value_or(false)) {
                        uint16_t port = ctx.GetMixedPort();
                        core::SysProxy::Get().Enable(port, ToUtf8(v));
                        // 若代理守卫也在运行，重启守卫以应用新 bypass
                        if (ctx.verge().enable_proxy_guard.value_or(false)) {
                            uint32_t interval = static_cast<uint32_t>(
                                ctx.verge().proxy_guard_interval.value_or(10));
                            core::SysProxy::Get().StopGuard();
                            core::SysProxy::Get().StartGuard(port, ToUtf8(v), interval);
                        }
                    }
                });

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// SubTask 19.3: User Interface 子页
// ============================================================

void SettingsPage::BuildUIPage(wxScrolledWindow* parent) {
    auto& verge = AppContext::Get().verge();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("用户界面"));

    // 主题模式选择
    {
        auto choices = MakeChoices({"light", "dark", "system"});
        int idx = FindChoiceIndex(choices, verge.theme_mode.value_or("system"));
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("主题模式"),
                      wxString::FromUTF8("亮色 / 暗色 / 跟随系统"),
                      choices, idx,
                      [this](const std::string& v) {
                          AppContext::Get().verge().theme_mode = v;
                          ThemeManager::Get().ApplyThemeMode(
                              ThemeManager::ParseThemeMode(v));
                          ThemeManager::Get().RefreshAll();
                          SaveVerge();
                      });
    }

    // 主题色输入
    AddTextCard(parent, sizer,
                wxString::FromUTF8("主题色"),
                wxString::FromUTF8("十六进制颜色（#RRGGBB）"),
                wxString::FromUTF8(verge.theme_color.value_or("#6750A4")),
                false, 120,
                [this](const wxString& v) {
                    std::string s = ToUtf8(v);
                    if (!config::is_hex_color(s)) return;  // 非法输入忽略
                    AppContext::Get().verge().theme_color = s;
                    ThemeManager::Get().ApplySeedColor(
                        ThemeManager::HexToColor(s));
                    ThemeManager::Get().RefreshAll();
                    SaveVerge();
                });

    // 语言选择
    {
        auto choices = MakeChoices({"zh-cn", "zh-tw", "en", "ru"});
        int idx = FindChoiceIndex(choices, verge.language.value_or("en"));
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("语言"),
                      wxString::FromUTF8("界面语言"),
                      choices, idx,
                      [this](const std::string& v) {
                          AppContext::Get().verge().language = v;
                          SaveVerge();
                      });
    }

    // 窗口置顶
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("窗口置顶"),
                  wxString::FromUTF8("主窗口始终保持在最前"),
                  verge.always_on_top.value_or(false),
                  [this](bool v) {
                      AppContext::Get().verge().always_on_top = v;
                      SaveVerge();
                      // 找到主窗口并应用置顶
                      wxWindow* top = wxTheApp->GetTopWindow();
                      if (top) {
                          long style = top->GetWindowStyleFlag();
                          if (v) {
                              style |= wxSTAY_ON_TOP;
                          } else {
                              style &= ~wxSTAY_ON_TOP;
                          }
                          top->SetWindowStyleFlag(style);
                          top->Refresh();
                      }
                  });

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// SubTask 19.4: Clash Settings 子页
// ============================================================

void SettingsPage::BuildClashPage(wxScrolledWindow* parent) {
    auto& verge = AppContext::Get().verge();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("Clash 设置"));

    // 内核管理区域（选择/版本/下载/镜像源）
    BuildCoreManagementSection(parent, sizer);

    // 端口
    AddSpinCard(parent, sizer,
                wxString::FromUTF8("混合端口"),
                wxString::FromUTF8("mihomo mixed-port（修改后需重启核心）"),
                1, 65535,
                static_cast<int>(verge.verge_mixed_port.value_or(7890)),
                [this](int v) {
                    AppContext::Get().verge().verge_mixed_port =
                        static_cast<uint16_t>(v);
                    SaveVergeAndRestartCore();
                });

    // 随机端口
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("随机端口"),
                  wxString::FromUTF8("每次启动核心时随机选择端口（修改后需重启核心）"),
                  verge.enable_random_port.value_or(false),
                  [this](bool v) {
                      AppContext::Get().verge().enable_random_port = v;
                      SaveVergeAndRestartCore();
                  });

    // TUN 模式（需要管理员权限；服务模式下由服务进程提供权限）
    {
        auto* card = new SettingCard(
            parent, wxString::FromUTF8("TUN 模式"),
            wxString::FromUTF8("虚拟网卡模式，接管全局流量（修改后需重启核心）"));
        auto* btn = new wxToggleButton(card, wxID_ANY, wxEmptyString);
        btn->SetValue(verge.enable_tun_mode.value_or(false));
        btn->Bind(wxEVT_TOGGLEBUTTON, [this, btn](wxCommandEvent& e) {
            const bool want = e.GetInt() != 0;
            auto& ctx = AppContext::Get();
            const bool service_mode =
                ctx.verge().enable_service_mode.value_or(false);
            if (want && !service_mode && !core::IsProcessElevated()) {
                wxMessageBox(
                    wxString::FromUTF8(
                        "TUN 模式需要管理员权限。\n"
                        "请以管理员身份运行 ClashYuka，或改用服务模式。"),
                    wxString::FromUTF8("TUN 模式"),
                    wxOK | wxICON_WARNING, this);
                btn->SetValue(false);
                return;
            }
            ctx.verge().enable_tun_mode = want;
            SaveVergeAndRestartCore();
        });
        card->AddControl(btn);
        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // TUN 堆栈
    {
        auto choices = MakeChoices({"system", "gvisor", "mixed"});
        int idx = FindChoiceIndex(choices, verge.tun_stack.value_or("gvisor"));
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("TUN 堆栈"),
                      wxString::FromUTF8("TUN 模式的网络栈实现（修改后需重启核心）"),
                      choices, idx,
                      [this](const std::string& v) {
                          auto& ctx = AppContext::Get();
                          ctx.verge().tun_stack = v;
                          // TUN 未启用时只需保存，下次启动生效，避免无谓重启
                          if (ctx.verge().enable_tun_mode.value_or(false)) {
                              SaveVergeAndRestartCore();
                          } else {
                              SaveVerge();
                          }
                      });
    }

    // 日志级别
    {
        auto choices = MakeChoices({"silent", "trace", "debug", "info", "warn", "error"});
        int idx = FindChoiceIndex(choices, verge.app_log_level.value_or("info"));
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("Clash 日志级别"),
                      wxString::FromUTF8("mihomo 运行日志级别（修改后实时生效）"),
                      choices, idx,
                      [this](const std::string& v) {
                          AppContext::Get().verge().app_log_level = v;
                          SaveVerge();
                          // 通过 PATCH /configs 实时修改日志级别，无需重启核心
                          auto& ctx = AppContext::Get();
                          if (ctx.IsInitialized() && ctx.IsCoreRunning()) {
                              nlohmann::json patch = {{"log-level", v}};
                              ctx.api().PatchConfig(patch);
                          }
                      });
    }

    // 字段过滤
    AddSwitchCard(parent, sizer,
                  wxString::FromUTF8("Clash 字段过滤"),
                  wxString::FromUTF8("过滤订阅中的不必要字段"),
                  verge.enable_clash_fields.value_or(true),
                  [this](bool v) {
                      AppContext::Get().verge().enable_clash_fields = v;
                      SaveVerge();
                  });

    // 默认延迟测试 URL
    AddTextCard(parent, sizer,
                wxString::FromUTF8("默认延迟测试 URL"),
                wxString::FromUTF8("节点测速使用的 URL"),
                wxString::FromUTF8(verge.default_latency_test.value_or(
                    "https://www.gstatic.com/generate_204")),
                false, 280,
                [this](const wxString& v) {
                    AppContext::Get().verge().default_latency_test = ToUtf8(v);
                    SaveVerge();
                });

    // ---- 内存与性能 ----
    AddSubHeader(parent, sizer, wxString::FromUTF8("内存与性能"));

    // Go GC 百分比（GOGC）
    AddSpinCard(parent, sizer,
                wxString::FromUTF8("Go GC 百分比"),
                wxString::FromUTF8("0 = Go 默认值（100）；调低更省内存但更耗 CPU（普通模式，重启生效）"),
                0, 1000,
                verge.mihomo_gogc.value_or(0),
                [this](int v) {
                    AppContext::Get().verge().mihomo_gogc = v;
                    SaveVerge();
                    memory_apply_pending_ = true;
                    memory_apply_timer_.StartOnce(800);
                });

    // Go 内存上限（GOMEMLIMIT）
    AddSpinCard(parent, sizer,
                wxString::FromUTF8("Go 内存上限 (MiB)"),
                wxString::FromUTF8("0 = 不限制；给 Go 运行时的软上限（普通模式，重启生效）"),
                0, 65536,
                verge.mihomo_memlimit_mb.value_or(0),
                [this](int v) {
                    AppContext::Get().verge().mihomo_memlimit_mb = v;
                    SaveVerge();
                    memory_apply_pending_ = true;
                    memory_apply_timer_.StartOnce(800);
                });

    // 进程匹配（find-process-mode）
    {
        auto choices = MakeChoices({"跟随配置", "off", "strict", "always"});
        int idx = 0;
        if (verge.mihomo_find_process_mode) {
            const int found =
                FindChoiceIndex(choices, *verge.mihomo_find_process_mode);
            if (found > 0) idx = found;
        }
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("进程匹配"),
                      wxString::FromUTF8("find-process-mode；off 可减少内存占用（重启生效）"),
                      choices, idx,
                      [this](const std::string& v) {
                          auto& ctx = AppContext::Get();
                          if (v == "跟随配置") {
                              ctx.verge().mihomo_find_process_mode.reset();
                          } else {
                              ctx.verge().mihomo_find_process_mode = v;
                          }
                          SaveVergeAndRestartCore();
                      });
    }

    // Sniffer
    {
        auto choices = MakeChoices({"跟随配置", "开启", "关闭"});
        int idx = 0;
        if (verge.mihomo_sniffer_enable.has_value()) {
            idx = *verge.mihomo_sniffer_enable ? 1 : 2;
        }
        AddChoiceCard(parent, sizer,
                      wxString::FromUTF8("Sniffer"),
                      wxString::FromUTF8("域名嗅探；关闭可减少少量内存占用（重启生效）"),
                      choices, idx,
                      [this](const std::string& v) {
                          auto& ctx = AppContext::Get();
                          if (v == "跟随配置") {
                              ctx.verge().mihomo_sniffer_enable.reset();
                          } else {
                              ctx.verge().mihomo_sniffer_enable = (v == "开启");
                          }
                          SaveVergeAndRestartCore();
                      });
    }

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// 外部控制器子页已移除（端口策略 / Web UI / Secret / PAC 均未接入实际逻辑）
// ============================================================

// ============================================================
// 快捷键子页
// ============================================================

void SettingsPage::BuildHotkeysPage(wxScrolledWindow* parent) {
    auto& verge = AppContext::Get().verge();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("快捷键"));

    // 热键编辑
    {
        wxString initial = JoinLines(verge.hotkeys.value_or(
            std::vector<std::string>{}));
        AddTextCard(parent, sizer,
                    wxString::FromUTF8("热键"),
                    wxString::FromUTF8("每行一个，格式：功能,键位（修改后立即生效）"),
                    initial, true, 320,
                    [this](const wxString& v) {
                        AppContext::Get().verge().hotkeys = SplitLines(v);
                        SaveVerge();
                        // 实时重新注册热键（需在主线程，这里已经是主线程回调）
                        core::HotkeyManager::Get().LoadFromConfig(AppContext::Get().verge());
                    });
    }

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// SubTask 19.7: Debug 子页
// ============================================================

void SettingsPage::BuildDebugPage(wxScrolledWindow* parent) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("调试"));

    // 配置目录（只读展示）
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("配置目录"),
            wxString::FromUTF8("应用配置文件所在路径"));
        auto* text = new wxTextCtrl(card, wxID_ANY,
            wxString::FromUTF8(utils::app_config_dir().string()),
            wxDefaultPosition, wxSize(320, -1), wxTE_READONLY);
        card->AddControl(text);
        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// SubTask 19.8: About 子页
// ============================================================

void SettingsPage::BuildAboutPage(wxScrolledWindow* parent) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    AddPageHeader(parent, sizer, wxString::FromUTF8("关于"));

    AddTextBlock(parent, sizer, wxString::FromUTF8(kAppVersion), 16);
    AddDescBlock(parent, sizer,
                 wxString::FromUTF8("clash-nyanpasu 的 C++ 重构版"));

    AddSubHeader(parent, sizer, wxString::FromUTF8("构建信息"));
    AddTextBlock(parent, sizer,
                 wxString::FromUTF8("编译器: ") + CompilerInfo());
    AddTextBlock(parent, sizer,
                 wxString::FromUTF8("构建日期: ") +
                 wxString::FromUTF8(__DATE__) + " " +
                 wxString::FromUTF8(__TIME__));

    AddSubHeader(parent, sizer, wxString::FromUTF8("致谢"));
    AddTextBlock(parent, sizer, wxString::FromUTF8("wxWidgets - GUI 框架"));
    AddTextBlock(parent, sizer, wxString::FromUTF8("mihomo - Clash 核心"));
    AddTextBlock(parent, sizer, wxString::FromUTF8("IXWebSocket - WebSocket 客户端"));
    AddTextBlock(parent, sizer, wxString::FromUTF8("nlohmann/json - JSON 库"));
    AddTextBlock(parent, sizer, wxString::FromUTF8("yaml-cpp - YAML 解析"));

    sizer->Add(0, 0, 0, wxBOTTOM, 24);
    parent->SetSizer(sizer);
    parent->FitInside();
}

// ============================================================
// 内核管理区域
// ============================================================

void SettingsPage::BuildCoreManagementSection(wxScrolledWindow* parent, wxSizer* sizer) {
    auto& verge = AppContext::Get().verge();
    auto& core_mgr = AppContext::Get().core_manager();

    // --- 内核选择卡片 ---
    {
        auto choices = MakeChoices({"mihomo", "mihomo-alpha"});
        int idx = FindChoiceIndex(choices, verge.clash_core.value_or("mihomo"));

        auto* card = new SettingCard(parent,
            wxString::FromUTF8("Clash 内核"),
            wxString::FromUTF8("选择 mihomo 核心类型（切换后自动重启核心）"));
        core_choice_ = new wxChoice(card, wxID_ANY, wxDefaultPosition, wxSize(200, -1),
                                    choices);
        if (idx >= 0) core_choice_->SetSelection(idx);
        core_choice_->Bind(wxEVT_CHOICE, &SettingsPage::OnCoreChoice, this);
        card->AddControl(core_choice_);
        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // --- 内核版本 + 检查更新 + 下载按钮 ---
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("内核版本"),
            wxString::FromUTF8("查看已安装版本并检查更新"));

        core_version_text_ = new wxStaticText(card, wxID_ANY,
            wxString::FromUTF8("已安装: ") + wxString::FromUTF8(
                core_mgr.GetInstalledVersion(verge.clash_core.value_or("mihomo")).empty()
                ? "未知" : core_mgr.GetInstalledVersion(verge.clash_core.value_or("mihomo"))));
        core_latest_text_ = new wxStaticText(card, wxID_ANY,
            wxString::FromUTF8("最新版: 未查询"));
        card->AddControl(core_version_text_);
        card->AddControl(core_latest_text_);

        auto* check_btn = new wxButton(card, wxID_ANY,
            wxString::FromUTF8("检查更新"));
        check_btn->Bind(wxEVT_BUTTON, &SettingsPage::OnCheckCoreUpdate, this);
        card->AddControl(check_btn);

        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // --- 下载/更新按钮 + 进度条 ---
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("下载/更新内核"),
            wxString::FromUTF8("从 GitHub Release 下载最新内核"));

        core_download_btn_ = new wxButton(card, wxID_ANY,
            wxString::FromUTF8("下载内核"));
        core_download_btn_->Bind(wxEVT_BUTTON, &SettingsPage::OnDownloadCore, this);
        card->AddControl(core_download_btn_);

        core_download_progress_ = new wxGauge(card, wxID_ANY, 100,
            wxDefaultPosition, wxSize(200, 10));
        core_download_progress_->Hide();
        card->AddControl(core_download_progress_);

        core_status_text_ = new wxStaticText(card, wxID_ANY, wxEmptyString);
        card->AddControl(core_status_text_);

        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // --- 镜像源 ---
    AddTextCard(parent, sizer,
        wxString::FromUTF8("下载镜像源"),
        wxString::FromUTF8("GitHub 下载镜像地址（留空默认使用 https://gh-proxy.com）"),
        wxString::FromUTF8(verge.core_mirror_url.value_or("")),
        false, 280,
        [this](const wxString& v) {
            AppContext::Get().verge().core_mirror_url = ToUtf8(v);
            SaveVerge();
        });

    // --- GeoIP / GeoSite 状态查询 + 下载按钮 ---
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("GeoIP / GeoSite 数据集"),
            wxString::FromUTF8("mihomo GEOIP 规则依赖 Country.mmdb；GEOSITE 规则依赖 geosite.dat（数据源：MetaCubeX/meta-rules-dat）"));

        geoip_status_text_ = new wxStaticText(card, wxID_ANY,
            wxString::FromUTF8("本地: 未知"));
        geosite_status_text_ = new wxStaticText(card, wxID_ANY,
            wxString::FromUTF8("本地: 未知"));
        geoip_latest_text_ = new wxStaticText(card, wxID_ANY,
            wxString::FromUTF8("最新版: 未查询"));
        card->AddControl(geoip_status_text_);
        card->AddControl(geosite_status_text_);
        card->AddControl(geoip_latest_text_);

        auto* check_btn = new wxButton(card, wxID_ANY,
            wxString::FromUTF8("检查版本"));
        check_btn->Bind(wxEVT_BUTTON, &SettingsPage::OnCheckGeoUpdate, this);
        card->AddControl(check_btn);

        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // --- GeoIP 下载按钮 + 进度 ---
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("下载 Country.mmdb"),
            wxString::FromUTF8("GEOIP 规则所需的 IP 地理位置数据库（MaxMind 格式）"));

        geoip_download_btn_ = new wxButton(card, wxID_ANY,
            wxString::FromUTF8("下载 GeoIP"));
        geoip_download_btn_->Bind(wxEVT_BUTTON, &SettingsPage::OnDownloadGeoIP, this);
        card->AddControl(geoip_download_btn_);

        geoip_download_progress_ = new wxGauge(card, wxID_ANY, 100,
            wxDefaultPosition, wxSize(200, 10));
        geoip_download_progress_->Hide();
        card->AddControl(geoip_download_progress_);

        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // --- GeoSite 下载按钮 + 进度 ---
    {
        auto* card = new SettingCard(parent,
            wxString::FromUTF8("下载 geosite.dat"),
            wxString::FromUTF8("GEOSITE 规则所需的站点分类数据库"));

        geosite_download_btn_ = new wxButton(card, wxID_ANY,
            wxString::FromUTF8("下载 GeoSite"));
        geosite_download_btn_->Bind(wxEVT_BUTTON, &SettingsPage::OnDownloadGeoSite, this);
        card->AddControl(geosite_download_btn_);

        geosite_download_progress_ = new wxGauge(card, wxID_ANY, 100,
            wxDefaultPosition, wxSize(200, 10));
        geosite_download_progress_->Hide();
        card->AddControl(geosite_download_progress_);

        geoip_action_status_ = new wxStaticText(card, wxID_ANY, wxEmptyString);
        card->AddControl(geoip_action_status_);

        sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // 初始化一次本地文件状态
    UpdateGeoAssetStatus();
}

void SettingsPage::OnCoreChoice(wxCommandEvent& event) {
    auto& verge = AppContext::Get().verge();
    verge.clash_core = ToUtf8(event.GetString());
    SaveVerge();
    // 重启核心
    AppContext::Get().RestartCore();
    // 更新版本显示
    if (core_version_text_) {
        auto ver = AppContext::Get().core_manager().GetInstalledVersion(verge.clash_core.value_or("mihomo"));
        core_version_text_->SetLabel(wxString::FromUTF8("已安装: ") +
            wxString::FromUTF8(ver.empty() ? "未知" : ver));
    }
}

void SettingsPage::OnCheckCoreUpdate(wxCommandEvent& event) {
    if (!core_latest_text_) return;
    core_latest_text_->SetLabel(wxString::FromUTF8("最新版: 查询中..."));

    auto& verge = AppContext::Get().verge();
    std::string core_name = verge.clash_core.value_or("mihomo");
    std::string mirror = verge.core_mirror_url.value_or("https://gh-proxy.com");

    std::thread([this, core_name, mirror]() {
        auto info = clash::CoreDownloader::CheckLatestVersion(core_name, mirror);
        wxTheApp->CallAfter([this, info]() {
            if (!IsBeingDeleted()) {
                if (info) {
                    core_latest_text_->SetLabel(
                        wxString::FromUTF8("最新版: ") + wxString::FromUTF8(info->version));
                    // 更新下载按钮文本
                    if (core_download_btn_) {
                        auto& verge = AppContext::Get().verge();
                        auto installed = AppContext::Get().core_manager().GetInstalledVersion(
                            verge.clash_core.value_or("mihomo"));
                        if (installed.empty()) {
                            core_download_btn_->SetLabel(wxString::FromUTF8("下载内核"));
                        } else if (clash::CoreManager::CompareVersions(installed, info->version) < 0) {
                            core_download_btn_->SetLabel(wxString::FromUTF8("更新内核"));
                        } else {
                            core_download_btn_->SetLabel(wxString::FromUTF8("已是最新"));
                        }
                    }
                } else {
                    core_latest_text_->SetLabel(wxString::FromUTF8("最新版: 查询失败"));
                }
            }
        });
    }).detach();
}

void SettingsPage::OnDownloadCore(wxCommandEvent& event) {
    if (!core_download_btn_) return;

    core_download_btn_->Disable();
    if (core_download_progress_) {
        core_download_progress_->Show();
        core_download_progress_->SetValue(0);
    }
    if (core_status_text_) {
        core_status_text_->SetLabel(wxString::FromUTF8("准备下载..."));
    }

    auto& verge = AppContext::Get().verge();
    std::string core_name = verge.clash_core.value_or("mihomo");
    std::string mirror = verge.core_mirror_url.value_or("https://gh-proxy.com");

    std::thread([this, core_name, mirror]() {
        // 查询最新版本
        auto info = clash::CoreDownloader::CheckLatestVersion(core_name, mirror);
        if (!info) {
            wxTheApp->CallAfter([this]() {
                if (!IsBeingDeleted()) {
                    if (core_status_text_)
                        core_status_text_->SetLabel(wxString::FromUTF8("查询版本失败（请检查镜像源设置）"));
                    if (core_download_btn_) core_download_btn_->Enable();
                    if (core_download_progress_) core_download_progress_->Hide();
                }
            });
            return;
        }

        // 更新状态
        wxTheApp->CallAfter([this]() {
            if (!IsBeingDeleted() && core_status_text_)
                core_status_text_->SetLabel(wxString::FromUTF8("下载中..."));
        });

        // 下载并安装
        auto path = clash::CoreDownloader::DownloadAndInstall(
            core_name, *info,
            [this](const clash::DownloadProgress& p) {
                wxTheApp->CallAfter([this, p]() {
                    if (!IsBeingDeleted() && core_download_progress_) {
                        core_download_progress_->SetValue(
                            p.percent >= 0 ? p.percent : 0);
                    }
                });
            },
            mirror);

        wxTheApp->CallAfter([this, path, core_name, info]() {
            if (IsBeingDeleted()) return;

            if (!path.empty()) {
                // 保存版本信息
                AppContext::Get().core_manager().SaveInstalledVersion(core_name, info->version);
                if (core_status_text_)
                    core_status_text_->SetLabel(wxString::FromUTF8("安装完成"));
                // 更新版本显示
                if (core_version_text_) {
                    core_version_text_->SetLabel(
                        wxString::FromUTF8("已安装: ") + wxString::FromUTF8(info->version));
                }
                // 重启内核
                AppContext::Get().RestartCore();
            } else {
                if (core_status_text_)
                    core_status_text_->SetLabel(wxString::FromUTF8("下载失败"));
            }

            if (core_download_btn_) core_download_btn_->Enable();
            if (core_download_progress_) core_download_progress_->Hide();
        });
    }).detach();
}

// ============================================================
// GeoIP / GeoSite 数据集管理
// ============================================================

// 查询本地 Country.mmdb / geosite.dat 文件状态并更新 UI
void SettingsPage::UpdateGeoAssetStatus() {
    namespace fs = std::filesystem;
    auto& resources = core::Resources::Get();

    if (geoip_status_text_) {
        fs::path p = resources.GetGeoIPPath();
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
            auto sz = fs::file_size(p, ec);
            wxString label = wxString::FromUTF8("GeoIP: ") +
                wxString::Format(wxT("%.1f MB"), sz / 1024.0 / 1024.0);
            geoip_status_text_->SetLabel(label);
            geoip_status_text_->SetForegroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        } else {
            geoip_status_text_->SetLabel(wxString::FromUTF8("GeoIP: 未下载"));
            geoip_status_text_->SetForegroundColour(wxColour(255, 149, 0));
        }
    }
    if (geosite_status_text_) {
        fs::path p = resources.GetGeoSitePath();
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
            auto sz = fs::file_size(p, ec);
            wxString label = wxString::FromUTF8("GeoSite: ") +
                wxString::Format(wxT("%.1f MB"), sz / 1024.0 / 1024.0);
            geosite_status_text_->SetLabel(label);
            geosite_status_text_->SetForegroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        } else {
            geosite_status_text_->SetLabel(wxString::FromUTF8("GeoSite: 未下载"));
            geosite_status_text_->SetForegroundColour(wxColour(255, 149, 0));
        }
    }
}

void SettingsPage::OnCheckGeoUpdate(wxCommandEvent& event) {
    if (geoip_latest_text_) {
        geoip_latest_text_->SetLabel(wxString::FromUTF8("最新版: 查询中..."));
    }
    auto& verge = AppContext::Get().verge();
    std::string mirror = verge.core_mirror_url.value_or("https://gh-proxy.com");

    std::thread([this, mirror]() {
        auto info = clash::CoreDownloader::CheckLatestGeoAssets(mirror);
        wxTheApp->CallAfter([this, info]() {
            if (IsBeingDeleted()) return;
            if (info) {
                geoip_latest_text_->SetLabel(
                    wxString::FromUTF8("最新版: ") + wxString::FromUTF8(info->version));
                if (geoip_action_status_) {
                    geoip_action_status_->SetLabel(
                        wxString::FromUTF8("就绪，可点击下载 GeoIP/GeoSite"));
                }
            } else {
                geoip_latest_text_->SetLabel(
                    wxString::FromUTF8("最新版: 查询失败（请检查镜像源）"));
            }
        });
    }).detach();
}

void SettingsPage::OnDownloadGeoIP(wxCommandEvent& event) {
    if (!geoip_download_btn_) return;
    geoip_download_btn_->Disable();
    if (geosite_download_btn_) geosite_download_btn_->Disable();
    if (geoip_download_progress_) {
        geoip_download_progress_->Show();
        geoip_download_progress_->SetValue(0);
    }
    if (geoip_action_status_) {
        geoip_action_status_->SetLabel(wxString::FromUTF8("查询版本..."));
    }

    auto& verge = AppContext::Get().verge();
    std::string mirror = verge.core_mirror_url.value_or("https://gh-proxy.com");

    std::thread([this, mirror]() {
        auto info = clash::CoreDownloader::CheckLatestGeoAssets(mirror);
        if (!info) {
            wxTheApp->CallAfter([this]() {
                if (IsBeingDeleted()) return;
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(wxString::FromUTF8("查询版本失败"));
                if (geoip_download_btn_) geoip_download_btn_->Enable();
                if (geosite_download_btn_) geosite_download_btn_->Enable();
                if (geoip_download_progress_) geoip_download_progress_->Hide();
            });
            return;
        }

        wxTheApp->CallAfter([this]() {
            if (!IsBeingDeleted() && geoip_action_status_)
                geoip_action_status_->SetLabel(wxString::FromUTF8("下载 Country.mmdb 中..."));
        });

        auto path = clash::CoreDownloader::DownloadGeoIP(
            *info,
            [this](const clash::DownloadProgress& p) {
                wxTheApp->CallAfter([this, p]() {
                    if (!IsBeingDeleted() && geoip_download_progress_) {
                        geoip_download_progress_->SetValue(
                            p.percent >= 0 ? p.percent : 0);
                    }
                });
            },
            mirror);

        wxTheApp->CallAfter([this, path, info]() {
            if (IsBeingDeleted()) return;

            if (!path.empty()) {
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(
                        wxString::FromUTF8("GeoIP 安装完成 v") +
                        wxString::FromUTF8(info->version));
                // 内核已运行则重启以应用新 MMDB
                if (AppContext::Get().IsCoreRunning()) {
                    AppContext::Get().RestartCore();
                }
            } else {
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(
                        wxString::FromUTF8("GeoIP 下载失败（请检查镜像源）"));
            }

            UpdateGeoAssetStatus();
            if (geoip_download_btn_) geoip_download_btn_->Enable();
            if (geosite_download_btn_) geosite_download_btn_->Enable();
            if (geoip_download_progress_) geoip_download_progress_->Hide();
        });
    }).detach();
}

void SettingsPage::OnDownloadGeoSite(wxCommandEvent& event) {
    if (!geosite_download_btn_) return;
    geosite_download_btn_->Disable();
    if (geoip_download_btn_) geoip_download_btn_->Disable();
    if (geosite_download_progress_) {
        geosite_download_progress_->Show();
        geosite_download_progress_->SetValue(0);
    }
    if (geoip_action_status_) {
        geoip_action_status_->SetLabel(wxString::FromUTF8("查询版本..."));
    }

    auto& verge = AppContext::Get().verge();
    std::string mirror = verge.core_mirror_url.value_or("https://gh-proxy.com");

    std::thread([this, mirror]() {
        auto info = clash::CoreDownloader::CheckLatestGeoAssets(mirror);
        if (!info) {
            wxTheApp->CallAfter([this]() {
                if (IsBeingDeleted()) return;
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(wxString::FromUTF8("查询版本失败"));
                if (geoip_download_btn_) geoip_download_btn_->Enable();
                if (geosite_download_btn_) geosite_download_btn_->Enable();
                if (geosite_download_progress_) geosite_download_progress_->Hide();
            });
            return;
        }

        wxTheApp->CallAfter([this]() {
            if (!IsBeingDeleted() && geoip_action_status_)
                geoip_action_status_->SetLabel(wxString::FromUTF8("下载 geosite.dat 中..."));
        });

        auto path = clash::CoreDownloader::DownloadGeoSite(
            *info,
            [this](const clash::DownloadProgress& p) {
                wxTheApp->CallAfter([this, p]() {
                    if (!IsBeingDeleted() && geosite_download_progress_) {
                        geosite_download_progress_->SetValue(
                            p.percent >= 0 ? p.percent : 0);
                    }
                });
            },
            mirror);

        wxTheApp->CallAfter([this, path, info]() {
            if (IsBeingDeleted()) return;

            if (!path.empty()) {
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(
                        wxString::FromUTF8("GeoSite 安装完成 v") +
                        wxString::FromUTF8(info->version));
                if (AppContext::Get().IsCoreRunning()) {
                    AppContext::Get().RestartCore();
                }
            } else {
                if (geoip_action_status_)
                    geoip_action_status_->SetLabel(
                        wxString::FromUTF8("GeoSite 下载失败（请检查镜像源）"));
            }

            UpdateGeoAssetStatus();
            if (geoip_download_btn_) geoip_download_btn_->Enable();
            if (geosite_download_btn_) geosite_download_btn_->Enable();
            if (geosite_download_progress_) geosite_download_progress_->Hide();
        });
    }).detach();
}

} // namespace clash_yuka::ui
