// ClashYuka Clash 核心进程管理
// 负责 mihomo 等核心子进程的启动 / 停止 / 重启 / 退出监控 / 自动重启。
// 对应 Rust 版 core/clash/core.rs 中 Instance::Child 分支的核心生命周期逻辑
// （去除 service / elevated / async runtime 复杂分支，保留子进程直启语义）。
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/types.h> // pid_t
#endif

namespace clash_yuka::clash {

// Clash 核心子进程管理器（线程安全）。
//
// 生命周期：
//   Start() -> [运行中] -> Stop() / Restart()
//                       -> 意外退出 -> TryAutoRestart()（带退避，最多 N 次）
//
// 回调（在内部线程中被调用，调用方需自行处理线程同步）：
//   - on_log：每行 stdout/stderr 输出
//   - on_exit：进程退出（exit_code；-1 表示无法获取）
class ClashProcess {
public:
    // 日志回调：参数为去掉换行符的一行输出
    using LogCallback = std::function<void(const std::string&)>;
    // 退出回调：参数为退出码（-1 表示无法获取）
    using ExitCallback = std::function<void(int)>;

    ClashProcess();
    ~ClashProcess();

    // 禁止拷贝与赋值（持有进程句柄与线程）
    ClashProcess(const ClashProcess&) = delete;
    ClashProcess& operator=(const ClashProcess&) = delete;

    // 启动核心。
    // 命令行：<core_path> -d <config_dir> -f <config_file>
    // 成功返回 true；失败时若 err 非空则写入错误描述。
    // 重复调用：若已在运行，返回 false（err="already running"）。
    bool Start(const std::filesystem::path& core_path,
               const std::filesystem::path& config_dir,
               const std::filesystem::path& config_file,
               std::string* err = nullptr);

    // 停止核心。
    // mihomo 无优雅关闭 IPC，故 Windows 上直接 TerminateProcess；
    // 调用后会等待监控线程退出。成功返回 true。
    // 若未运行，返回 false（err="not running"）。
    bool Stop(std::string* err = nullptr);

    // 重启核心：Stop + Start（复用上次的启动参数）。
    bool Restart(std::string* err = nullptr);

    // 设置 mihomo 的 Go 运行时内存参数（0 表示不设置）。
    // 需在 Start 前调用；仅影响后续启动的子进程（含自动重启）。
    void set_go_memory_env(int gogc, int memlimit_mb);

    // 是否运行中。
    bool IsRunning() const;

    // 当前进程 PID（未运行返回 0）。
    uint32_t pid() const;

    // 设置日志回调（可在 Start 前调用；运行中调用也安全）。
    void set_on_log(LogCallback cb);
    // 设置退出回调。
    void set_on_exit(ExitCallback cb);

    // 根据核心名（如 "mihomo" / "mihomo-alpha" / "clash-rs"）解析可执行文件路径。
    // 搜索 utils::app_resources_dir()；Windows 自动追加 ".exe"。
    // 找不到返回空 path。
    static std::filesystem::path ResolveCorePath(const std::string& core_name);

    // 自动重启最大次数（默认 5）。仅在 Start 成功后的意外退出场景生效。
    void set_max_restart_count(int n);
    int max_restart_count() const;

private:
    // 调用方持 mutex_ 的运行状态检查
    bool IsRunningLocked() const;
    // 启动退出监控线程与日志读取线程（假设旧线程已回收）。
    void StartMonitorThreads();
    // 退出监控线程入口。
    void ExitMonitor();
    // 日志读取线程入口：按行分发到 on_log_。
    void LogReader();
    // 尝试自动重启（带指数退避：1/2/4/8/16s）。在独立 detached 线程中运行。
    void TryAutoRestart();
    // 关闭并清理平台句柄（调用方持 mutex_）。
    void CleanupHandlesLocked();
    // 分发一行日志到回调（持锁读取 on_log_）。
    void DispatchLog(const std::string& line);
    // Start 实现：假定调用方已持有 op_mutex_。
    bool StartImpl(const std::filesystem::path& core_path,
                   const std::filesystem::path& config_dir,
                   const std::filesystem::path& config_file,
                   std::string* err);
    // Stop 实现：假定调用方已持有 op_mutex_。
    bool StopImpl(std::string* err);

    // data mutex：保护 on_*、句柄、启动参数、计数等共享状态
    mutable std::mutex mutex_;
    // op mutex：序列化 Start/Stop/Restart/TryAutoRestart 的生命周期操作，
    // 避免 monitor 线程 join 与新 Start 之间的竞态。
    std::mutex op_mutex_;
    LogCallback on_log_;
    ExitCallback on_exit_;

    // 上次启动参数（Restart / 自动重启复用）
    std::filesystem::path core_path_;
    std::filesystem::path config_dir_;
    std::filesystem::path config_file_;

    int max_restart_count_ = 5;
    int restart_count_ = 0;
    // Go 运行时内存参数（0 = 不覆盖）
    int go_gc_ = 0;
    int go_memlimit_mb_ = 0;
    // 用户主动 Stop 标志：true 时退出监控不触发自动重启
    bool user_stopped_ = false;

    // ---- 平台特定句柄 / 标识 ----
#ifdef _WIN32
    HANDLE process_handle_ = nullptr;   // 子进程主句柄（Wait/Terminate 用）
    HANDLE stdout_write_ = nullptr;     // 子进程 stdout/stderr 写端
    HANDLE stdout_read_ = nullptr;      // 父进程读端
    uint32_t pid_ = 0;
#else
    pid_t pid_ = -1;
    int stdout_pipe_[2] = {-1, -1};     // [0]=读端 [1]=写端
#endif

    std::thread exit_thread_;
    std::thread log_thread_;
    // Stop 进行中标志：日志读取线程据此区分正常关闭
    std::atomic<bool> stopping_{false};
};

} // namespace clash_yuka::clash
