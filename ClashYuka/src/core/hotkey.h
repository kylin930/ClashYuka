// ClashYuka 全局热键管理（Phase 9 Task 21）
//
// 基于 Windows RegisterHotKey API 实现全局热键注册。热键消息通过一个隐藏的
// wxFrame 接收 WM_HOTKEY，转发到 OnHotkey 分发到对应动作。
//
// 支持的动作：open_main_window / restart_core / toggle_system_proxy /
// mode_rule / mode_global / mode_direct
//
// 热键格式："Ctrl+Shift+M"（修饰键用 + 分隔，末尾为虚拟键名）。
// 所有方法须在主线程调用（wxWidgets UI 操作非线程安全）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clash_yuka::config {
struct IVerge;
}

namespace clash_yuka::core {

class HotkeyManager {
public:
    static HotkeyManager& Get();

    // 禁止拷贝/移动
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;
    HotkeyManager(HotkeyManager&&) = delete;
    HotkeyManager& operator=(HotkeyManager&&) = delete;

    // 从 IVerge::hotkeys 加载并注册所有热键。
    // hotkeys 格式：["open_main_window,Ctrl+Shift+M", "restart_core,Ctrl+Shift+R"]
    // 会先注销所有已注册热键再重新注册。
    void LoadFromConfig(const config::IVerge& verge);

    // 注册单个热键。func 为动作名，key_combo 为 "Ctrl+Shift+M" 格式。
    // 返回是否注册成功。
    bool Register(const std::string& func, const std::string& key_combo);

    // 注销所有热键
    void UnregisterAll();

private:
    HotkeyManager();
    ~HotkeyManager();

    // 解析 "Ctrl+Shift+M" -> MOD_CONTROL | MOD_SHIFT + 'M'
    // 成功返回 true，mod 与 vk 写入输出参数。
    bool ParseCombo(const std::string& combo, uint32_t& mod, uint32_t& vk);

    // 热键消息处理：根据 id 查找已注册的动作并执行
    void OnHotkey(int id);

    // 确保隐藏消息窗口已创建（主线程调用）
    void EnsureWindow();

    struct RegisteredHotkey {
        int id;
        std::string func;
    };
    std::vector<RegisteredHotkey> registered_;
    int next_id_ = 0xC000;  // Windows 热键 ID 从 0xC000 开始

    // 隐藏窗口的 opaque 指针（实际类型为 HotkeyWindow*，定义在 .cpp 中）
    void* window_ = nullptr;
};

} // namespace clash_yuka::core
