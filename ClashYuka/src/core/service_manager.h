// 服务模式管理器（GUI 端）
// 通过调用 clashyuka-service.exe 的命令行接口来管理服务
// 安装/卸载/启动/停止需要管理员权限，通过 ShellExecute "runas" 提权

#pragma once

#include <string>
#include <filesystem>

namespace clash_yuka::core {

enum class ServiceStatus {
    NotInstalled,
    Running,
    Stopped,
    Starting,
    Stopping,
    Unknown,
};

struct ServiceState {
    ServiceStatus status = ServiceStatus::NotInstalled;
    std::string error;
};

class ServiceManager {
public:
    static ServiceManager& Get();

    std::filesystem::path service_exe() const;

    ServiceState QueryStatus() const;

    bool Install() const;
    bool Uninstall() const;
    bool Start() const;
    bool Stop() const;
    bool Restart() const;

    bool IsInstalled() const;
    bool IsRunning() const;

    // 最近一次 Install/Uninstall/Start/Stop/Restart 失败的原因。
    // 成功时返回空字符串。可用于在 UI 上展示具体错误。
    std::string last_error() const;

private:
    ServiceManager() = default;
    ~ServiceManager() = default;

    int RunElevated(const std::wstring& args) const;
    std::string RunStatus() const;
    std::string InterpretElevateCode(int code) const;

    // mutable：const 方法中通过 RunElevated 设置
    mutable std::string last_error_;
};

} // namespace clash_yuka::core
