// ClashYuka 开机自启管理（Phase 9 Task 23）
//
// 通过 Windows 注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 实现
// 开机自启。值名 "ClashYuka"，值为可执行文件完整路径。
//
// 对应 clash-nyanpasu 的 autostart 逻辑（移植到原生 Windows 注册表方案）。
#pragma once

#include <string>

namespace clash_yuka::core {

class AutoStart {
public:
    static AutoStart& Get();

    // 禁止拷贝/移动
    AutoStart(const AutoStart&) = delete;
    AutoStart& operator=(const AutoStart&) = delete;
    AutoStart(AutoStart&&) = delete;
    AutoStart& operator=(AutoStart&&) = delete;

    // 启用开机自启：向注册表 Run 项写入可执行文件路径。成功返回 true。
    bool Enable();

    // 禁用开机自启：从注册表 Run 项删除值。成功返回 true。
    bool Disable();

    // 查询当前是否已启用开机自启。
    bool IsEnabled() const;

private:
    AutoStart() = default;
    ~AutoStart() = default;

    // 获取当前可执行文件的完整路径（含 .exe）。
    std::string GetAppPath() const;
};

} // namespace clash_yuka::core
