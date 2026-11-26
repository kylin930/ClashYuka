// ClashYuka 全局热键管理实现
#include "core/hotkey.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/frame.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/app_context.h"
#include "core/config/verge_config.h"
#include "core/sysopt.h"

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace clash_yuka::core {

namespace {

#ifdef __WXMSW__

// 隐藏窗口：仅用于接收 WM_HOTKEY 消息，不显示任何 UI。
class HotkeyWindow : public wxFrame {
public:
    HotkeyWindow()
        : wxFrame(nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                  wxFRAME_NO_TASKBAR) {
        Hide();  // 确保不可见
    }

    void SetHandler(std::function<void(int)> handler) {
        handler_ = std::move(handler);
    }

    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override {
        if (message == WM_HOTKEY) {
            int id = static_cast<int>(wParam);
            if (handler_) handler_(id);
            return 0;
        }
        return wxFrame::MSWWindowProc(message, wParam, lParam);
    }

private:
    std::function<void(int)> handler_;
};

#endif // __WXMSW__

// 字符串转大写（用于键名比较）
std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// 去除首尾空白
std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// 按 ',' 分割 "func,key_combo" -> {func, key_combo}
// 仅在第一个 ',' 处分割，以支持键名中不含逗号的情况
std::pair<std::string, std::string> SplitFuncKey(const std::string& entry) {
    auto pos = entry.find(',');
    if (pos == std::string::npos) return {Trim(entry), ""};
    return {Trim(entry.substr(0, pos)), Trim(entry.substr(pos + 1))};
}

} // namespace

HotkeyManager& HotkeyManager::Get() {
    static HotkeyManager instance;
    return instance;
}

HotkeyManager::HotkeyManager() = default;

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

void HotkeyManager::EnsureWindow() {
#ifdef __WXMSW__
    if (window_ != nullptr) return;
    auto* win = new HotkeyWindow();
    win->SetHandler([this](int id) { this->OnHotkey(id); });
    window_ = win;
#endif
}

bool HotkeyManager::ParseCombo(const std::string& combo, uint32_t& mod, uint32_t& vk) {
#ifdef __WXMSW__
    mod = 0;
    vk = 0;

    // 按 '+' 分割
    std::vector<std::string> parts;
    std::istringstream iss(combo);
    std::string part;
    while (std::getline(iss, part, '+')) {
        parts.push_back(Trim(part));
    }
    if (parts.empty()) return false;

    // 最后一项为虚拟键名，其余为修饰键
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string m = ToUpper(parts[i]);
        if (m == "CTRL" || m == "CONTROL") {
            mod |= MOD_CONTROL;
        } else if (m == "SHIFT") {
            mod |= MOD_SHIFT;
        } else if (m == "ALT" || m == "OPTION") {
            mod |= MOD_ALT;
        } else if (m == "WIN" || m == "SUPER" || m == "CMD" || m == "COMMAND" || m == "META") {
            mod |= MOD_WIN;
        } else {
            std::cerr << "[Hotkey] Unknown modifier: " << parts[i] << std::endl;
            return false;
        }
    }

    // 解析虚拟键
    std::string key = ToUpper(parts.back());
    if (key.empty()) return false;

    if (key.size() == 1) {
        // 单字符：A-Z 或 0-9
        char c = key[0];
        if (c >= 'A' && c <= 'Z') {
            vk = static_cast<uint32_t>(c);  // 'A' = 0x41
        } else if (c >= '0' && c <= '9') {
            vk = static_cast<uint32_t>(c);  // '0' = 0x30
        } else {
            std::cerr << "[Hotkey] Invalid key: " << key << std::endl;
            return false;
        }
    } else if (key.size() >= 2 && key[0] == 'F') {
        // 功能键 F1-F24
        int n = std::atoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) {
            vk = VK_F1 + (n - 1);
        } else {
            std::cerr << "[Hotkey] Invalid function key: " << key << std::endl;
            return false;
        }
    } else {
        // 特殊键名映射
        static const std::unordered_map<std::string, uint32_t> special_keys = {
            {"SPACE", VK_SPACE},
            {"ENTER", VK_RETURN},
            {"RETURN", VK_RETURN},
            {"TAB", VK_TAB},
            {"ESC", VK_ESCAPE},
            {"ESCAPE", VK_ESCAPE},
            {"HOME", VK_HOME},
            {"END", VK_END},
            {"PAGEUP", VK_PRIOR},
            {"PAGEDOWN", VK_NEXT},
            {"INSERT", VK_INSERT},
            {"DELETE", VK_DELETE},
            {"UP", VK_UP},
            {"DOWN", VK_DOWN},
            {"LEFT", VK_LEFT},
            {"RIGHT", VK_RIGHT},
            {"BACKSPACE", VK_BACK},
            {"CAPSLOCK", VK_CAPITAL},
            {"NUMLOCK", VK_NUMLOCK},
            {"SCROLLLOCK", VK_SCROLL},
            {"PRINTSCREEN", VK_SNAPSHOT},
            {"PAUSE", VK_PAUSE},
        };
        auto it = special_keys.find(key);
        if (it != special_keys.end()) {
            vk = it->second;
        } else {
            std::cerr << "[Hotkey] Unknown key: " << key << std::endl;
            return false;
        }
    }

    // 添加 MOD_NOREPEAT 防止按住时重复触发（Windows 7+）
    mod |= MOD_NOREPEAT;
    return true;
#else
    (void)combo;
    (void)mod;
    (void)vk;
    return false;
#endif
}

bool HotkeyManager::Register(const std::string& func, const std::string& key_combo) {
#ifdef __WXMSW__
    if (func.empty() || key_combo.empty()) return false;

    EnsureWindow();
    auto* win = static_cast<HotkeyWindow*>(window_);
    if (win == nullptr) return false;

    uint32_t mod = 0;
    uint32_t vk = 0;
    if (!ParseCombo(key_combo, mod, vk)) {
        std::cerr << "[Hotkey] Failed to parse combo: " << key_combo << std::endl;
        return false;
    }

    int id = next_id_++;
    HWND hwnd = static_cast<HWND>(win->GetHWND());
    if (!RegisterHotKey(hwnd, id, mod, vk)) {
        std::cerr << "[Hotkey] RegisterHotKey failed for " << func << " (" << key_combo
                  << "), error=" << GetLastError() << std::endl;
        return false;
    }

    registered_.push_back({id, func});
    return true;
#else
    (void)func;
    (void)key_combo;
    return false;
#endif
}

void HotkeyManager::UnregisterAll() {
#ifdef __WXMSW__
    if (window_ == nullptr) {
        registered_.clear();
        return;
    }
    auto* win = static_cast<HotkeyWindow*>(window_);
    HWND hwnd = static_cast<HWND>(win->GetHWND());
    for (const auto& h : registered_) {
        UnregisterHotKey(hwnd, h.id);
    }
    registered_.clear();
#endif
}

void HotkeyManager::LoadFromConfig(const clash_yuka::config::IVerge& verge) {
    // 先注销所有已注册热键
    UnregisterAll();

    if (!verge.hotkeys.has_value()) return;

    for (const auto& entry : verge.hotkeys.value()) {
        auto [func, key_combo] = SplitFuncKey(entry);
        if (func.empty() || key_combo.empty()) {
            std::cerr << "[Hotkey] Skipping invalid hotkey entry: " << entry << std::endl;
            continue;
        }
        if (!Register(func, key_combo)) {
            std::cerr << "[Hotkey] Failed to register: " << func << " = " << key_combo << std::endl;
        }
    }
}

void HotkeyManager::OnHotkey(int id) {
    // 查找对应动作
    std::string func;
    for (const auto& h : registered_) {
        if (h.id == id) {
            func = h.func;
            break;
        }
    }
    if (func.empty()) return;

    // 根据动作名分发
    if (func == "open_main_window") {
        // 在主线程显示并激活主窗口（WM_HOTKEY 本就在主线程，但用 CallAfter 确保安全）
        wxTheApp->CallAfter([]() {
            auto* top = wxTheApp->GetTopWindow();
            if (top) {
                auto* tlw = static_cast<wxTopLevelWindow*>(top);
                top->Show(true);
                if (tlw->IsIconized()) tlw->Iconize(false);
                top->Raise();
                top->SetFocus();
            }
        });
    } else if (func == "restart_core") {
        // 在 detached 线程中重启（通过 AppContext::RestartCore 重建 config.yaml，
        // 避免复用旧配置文件导致手动修改的 profile 不生效）
        std::thread([]() {
            auto& ctx = clash_yuka::AppContext::Get();
            std::string err = ctx.RestartCore();
            if (!err.empty()) {
                std::cerr << "[Hotkey] 重启内核失败: " << err << std::endl;
            }
        }).detach();
    } else if (func == "toggle_system_proxy") {
        // 在 detached 线程中切换系统代理
        std::thread([]() {
            auto& ctx = clash_yuka::AppContext::Get();
            auto& proxy = SysProxy::Get();
            if (proxy.IsEnabled()) {
                proxy.Disable();
            } else {
                uint16_t port = ctx.clash_info().port;
                std::string bypass;
                if (ctx.verge().system_proxy_bypass.has_value()) {
                    bypass = ctx.verge().system_proxy_bypass.value();
                }
                proxy.Enable(port, bypass);
            }
        }).detach();
    } else if (func == "mode_rule" || func == "mode_global" || func == "mode_direct") {
        // 在 detached 线程中切换 mihomo 模式
        std::string mode = func.substr(5);  // "rule" / "global" / "direct"
        std::thread([mode]() {
            auto& ctx = clash_yuka::AppContext::Get();
            nlohmann::json patch = {{"mode", mode}};
            ctx.api().PatchConfig(patch);
        }).detach();
    } else {
        std::cerr << "[Hotkey] Unknown action: " << func << std::endl;
    }
}

} // namespace clash_yuka::core
