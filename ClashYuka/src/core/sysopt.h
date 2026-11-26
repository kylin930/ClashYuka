// ClashYuka 系统代理管理（Phase 9 Task 22）
//
// 通过 Windows 注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings
// 管理系统级 HTTP/HTTPS 代理。支持代理守护（后台线程定时校验，被外部修改后自动恢复）。
//
// 注册表字段：
//   ProxyEnable  (DWORD)    : 0=禁用 / 1=启用
//   ProxyServer  (REG_SZ)   : "127.0.0.1:7890"
//   ProxyOverride (REG_SZ)  : bypass 列表（分号分隔）
//
// 修改后通过 InternetSetOption 通知系统刷新设置。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace clash_yuka::core {

// 当前进程是否以管理员（Windows）/ root（POSIX）权限运行。
// TUN 模式创建虚拟网卡需要该权限；服务模式下由服务进程提供权限。
bool IsProcessElevated();

class SysProxy {
public:
    static SysProxy& Get();

    // 禁止拷贝/移动
    SysProxy(const SysProxy&) = delete;
    SysProxy& operator=(const SysProxy&) = delete;
    SysProxy(SysProxy&&) = delete;
    SysProxy& operator=(SysProxy&&) = delete;

    // 启用系统代理：设置 HTTP/HTTPS 代理为 127.0.0.1:port。
    // bypass 为 bypass 列表（分号分隔），为空则不写 ProxyOverride。
    void Enable(uint16_t port, const std::string& bypass = "");

    // 禁用系统代理。
    void Disable();

    // 查询当前是否启用（基于内部状态，非实时读注册表）。
    bool IsEnabled() const { return enabled_; }

    // 获取当前代理端口。
    uint16_t GetPort() const { return port_; }

    // 代理守护：启动定时校验线程，每隔 interval_sec 秒检查系统代理是否被外部修改。
    // 若被修改则重新应用。调用方通过 verge.enable_proxy_guard 控制是否启用。
    void StartGuard(uint16_t port, const std::string& bypass, uint32_t interval_sec);
    // 停止代理守护线程。
    void StopGuard();

private:
    SysProxy();
    ~SysProxy();

    // Windows 注册表读写
    bool ReadRegistry(uint32_t& proxy_enable, std::string& proxy_server, std::string& bypass);
    bool WriteRegistry(bool enable, const std::string& server, const std::string& bypass);

    // 通知系统刷新代理设置（调用 InternetSetOption）
    void NotifySettingsChanged();

    // 代理守护线程入口
    void GuardLoop(uint32_t interval_sec);

    uint16_t port_ = 0;
    std::string bypass_;
    bool enabled_ = false;

    std::atomic<bool> guard_running_{false};
    std::thread guard_thread_;
};

} // namespace clash_yuka::core
