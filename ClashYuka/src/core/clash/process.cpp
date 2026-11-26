// ClashYuka Clash 核心进程管理实现
#include "process.h"

#include "core/utils/dirs.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace clash_yuka::clash {

namespace fs = std::filesystem;

#ifdef _WIN32

// 临时覆盖父进程环境变量（CreateProcess 继承父环境），析构时恢复。
// 用于给 mihomo 子进程设置 GOGC / GOMEMLIMIT。
class ScopedEnvOverride {
public:
    ScopedEnvOverride(const wchar_t* name, const std::wstring& value) : name_(name) {
        wchar_t buf[512] = {0};
        DWORD n = GetEnvironmentVariableW(name, buf, 512);
        had_ = (n > 0 && n < 512);
        if (had_) old_.assign(buf, n);
        SetEnvironmentVariableW(name, value.c_str());
    }
    ~ScopedEnvOverride() {
        SetEnvironmentVariableW(name_, had_ ? old_.c_str() : nullptr);
    }
    ScopedEnvOverride(const ScopedEnvOverride&) = delete;
    ScopedEnvOverride& operator=(const ScopedEnvOverride&) = delete;

private:
    const wchar_t* name_;
    bool had_ = false;
    std::wstring old_;
};

#endif

namespace {

#ifdef _WIN32

// 将 path 转换为以引号包裹的宽字符串（用于命令行拼接）。
// 路径中若含空格需用引号包裹；这里统一包裹以简化处理。
std::wstring QuotePath(const fs::path& p) {
    std::wstring s = p.wstring();
    std::wstring out;
    out.reserve(s.size() + 2);
    out.push_back(L'"');
    for (wchar_t c : s) {
        if (c == L'"') out.push_back(L'"');
        out.push_back(c);
    }
    out.push_back(L'"');
    return out;
}

// 拼接命令行："<core> -d <dir> -f <file>"
std::wstring BuildCommandLine(const fs::path& core_path,
                              const fs::path& config_dir,
                              const fs::path& config_file) {
    std::wstring cmd = QuotePath(core_path);
    cmd += L" -d ";
    cmd += QuotePath(config_dir);
    cmd += L" -f ";
    cmd += QuotePath(config_file);
    return cmd;
}

// 将 GetLastError 转为字符串
std::string WinError(DWORD code) {
    LPWSTR buf = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string out = "win32 error " + std::to_string(code);
    if (len > 0 && buf) {
        std::wstring ws(buf, len);
        out += ": ";
        out.reserve(out.size() + ws.size());
        for (wchar_t c : ws) {
            out.push_back(static_cast<char>(c & 0xFF));
        }
    }
    if (buf) LocalFree(buf);
    return out;
}

#endif // _WIN32

} // namespace

ClashProcess::ClashProcess() = default;

ClashProcess::~ClashProcess() {
    // 析构时若仍在运行，尝试停止并等待监控线程退出，避免句柄泄漏与悬空回调
    std::string err;
    Stop(&err);
    // TryAutoRestart 运行在 detached 线程中并捕获 this。Stop 已设置 stopping_=true，
    // 该线程会在 ≤100ms 内检测到并退出。此处短暂等待以确保其不再访问本对象，
    // 避免析构后的悬空访问。（若需立即析构，应在析构前显式 Stop 并确保无重启进行）
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void ClashProcess::set_on_log(LogCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    on_log_ = std::move(cb);
}

void ClashProcess::set_on_exit(ExitCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    on_exit_ = std::move(cb);
}

void ClashProcess::set_max_restart_count(int n) {
    std::lock_guard<std::mutex> lk(mutex_);
    max_restart_count_ = n < 0 ? 0 : n;
}

int ClashProcess::max_restart_count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return max_restart_count_;
}

bool ClashProcess::IsRunningLocked() const {
#ifdef _WIN32
    if (process_handle_ == nullptr) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(process_handle_, &code)) {
        return code == STILL_ACTIVE;
    }
    return false;
#else
    // 使用 kill(pid, 0) 探活而非 waitpid(WNOHANG)，避免与 ExitMonitor 的
    // 阻塞 waitpid 竞争回收子进程（会导致 ExitMonitor 收到 ECHILD）。
    if (pid_ <= 0) return false;
    return ::kill(pid_, 0) == 0;
#endif
}

bool ClashProcess::IsRunning() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return IsRunningLocked();
}

uint32_t ClashProcess::pid() const {
    std::lock_guard<std::mutex> lk(mutex_);
#ifdef _WIN32
    return pid_;
#else
    return pid_ > 0 ? static_cast<uint32_t>(pid_) : 0;
#endif
}

void ClashProcess::DispatchLog(const std::string& line) {
    LogCallback cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = on_log_;
    }
    if (cb) cb(line);
}

void ClashProcess::CleanupHandlesLocked() {
#ifdef _WIN32
    if (stdout_read_) { CloseHandle(stdout_read_); stdout_read_ = nullptr; }
    if (stdout_write_) { CloseHandle(stdout_write_); stdout_write_ = nullptr; }
    if (process_handle_) { CloseHandle(process_handle_); process_handle_ = nullptr; }
    pid_ = 0;
#else
    if (stdout_pipe_[0] != -1) { ::close(stdout_pipe_[0]); stdout_pipe_[0] = -1; }
    if (stdout_pipe_[1] != -1) { ::close(stdout_pipe_[1]); stdout_pipe_[1] = -1; }
    pid_ = -1;
#endif
}

bool ClashProcess::Start(const fs::path& core_path,
                         const fs::path& config_dir,
                         const fs::path& config_file,
                         std::string* err) {
    std::lock_guard<std::mutex> op(op_mutex_);
    // 用户主动 Start：重置自动重启计数（开启新一轮会话）
    {
        std::lock_guard<std::mutex> lk(mutex_);
        restart_count_ = 0;
    }
    return StartImpl(core_path, config_dir, config_file, err);
}

void ClashProcess::set_go_memory_env(int gogc, int memlimit_mb) {
    std::lock_guard<std::mutex> lk(mutex_);
    go_gc_ = gogc > 0 ? gogc : 0;
    go_memlimit_mb_ = memlimit_mb > 0 ? memlimit_mb : 0;
}

bool ClashProcess::StartImpl(const fs::path& core_path,
                             const fs::path& config_dir,
                             const fs::path& config_file,
                             std::string* err) {
    std::error_code ec;
    if (!fs::exists(core_path, ec)) {
        if (err) *err = "core binary not found: " + core_path.string();
        return false;
    }
    if (!fs::exists(config_file, ec)) {
        if (err) *err = "config file not found: " + config_file.string();
        return false;
    }

    int go_gc = 0;
    int go_memlimit = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (IsRunningLocked()) {
            if (err) *err = "already running";
            return false;
        }
        // 记录启动参数（Restart / 自动重启复用）
        core_path_ = core_path;
        config_dir_ = config_dir;
        config_file_ = config_file;
        user_stopped_ = false;
        stopping_.store(false, std::memory_order_release);
        // Go 运行时内存参数（仅影响子进程）
        go_gc = go_gc_;
        go_memlimit = go_memlimit_mb_;
        // 注意：restart_count_ 由调用方（Start/Restart/TryAutoRestart）管理，
        // 此处不重置，以免破坏 TryAutoRestart 的失败重试计数。
    }

#ifdef _WIN32
    // 创建匿名管道用于捕获子进程 stdout/stderr
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_h = nullptr;
    HANDLE write_h = nullptr;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) {
        if (err) *err = WinError(GetLastError());
        return false;
    }
    // 读端由父进程持有，不可被子进程继承
    if (!SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0)) {
        DWORD e = GetLastError();
        CloseHandle(read_h);
        CloseHandle(write_h);
        if (err) *err = WinError(e);
        return false;
    }

    std::wstring cmd = BuildCommandLine(core_path, config_dir, config_file);
    // CreateProcess 会修改命令行缓冲，故使用可变副本
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nullptr;
    si.hStdOutput = write_h;
    si.hStdError = write_h;

    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW：避免弹出控制台窗口
    // CREATE_NEW_PROCESS_GROUP：便于后续可选的控制台信号控制
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;

    // Go 运行时内存参数通过环境变量传给子进程；作用域结束后恢复父进程环境。
    std::unique_ptr<ScopedEnvOverride> gogc_env;
    std::unique_ptr<ScopedEnvOverride> memlimit_env;
    if (go_gc > 0) {
        gogc_env = std::make_unique<ScopedEnvOverride>(
            L"GOGC", std::to_wstring(go_gc));
    }
    if (go_memlimit > 0) {
        memlimit_env = std::make_unique<ScopedEnvOverride>(
            L"GOMEMLIMIT", std::to_wstring(go_memlimit) + L"MiB");
    }

    if (!CreateProcessW(nullptr,
                        cmd_buf.data(),
                        nullptr, nullptr,
                        TRUE,   // 继承句柄
                        flags,
                        nullptr, // 继承父进程环境
                        nullptr, // 继承父进程工作目录
                        &si, &pi)) {
        DWORD e = GetLastError();
        CloseHandle(read_h);
        CloseHandle(write_h);
        if (err) *err = WinError(e);
        return false;
    }
    // 关闭子进程主线程句柄（不需要）
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        stdout_read_ = read_h;
        stdout_write_ = write_h; // 保留父端副本，子进程退出后由 ExitMonitor 关闭以触发日志 EOF
        process_handle_ = pi.hProcess;
        pid_ = pi.dwProcessId;
    }
#else
    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        if (err) *err = std::string("pipe failed: ") + std::strerror(errno);
        return false;
    }

    pid_t child = ::fork();
    if (child < 0) {
        int e = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        if (err) *err = std::string("fork failed: ") + std::strerror(e);
        return false;
    }
    if (child == 0) {
        // 子进程：重定向 stdout/stderr 到管道写端，再 exec
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
            ::close(pipefd[1]);
        }
        // Go 运行时内存参数（仅影响子进程）
        if (go_gc > 0) {
            const std::string v = std::to_string(go_gc);
            ::setenv("GOGC", v.c_str(), 1);
        }
        if (go_memlimit > 0) {
            const std::string v = std::to_string(go_memlimit) + "MiB";
            ::setenv("GOMEMLIMIT", v.c_str(), 1);
        }
        std::string core_s = core_path.string();
        std::string dir_s = config_dir.string();
        std::string file_s = config_file.string();
        std::vector<char*> argv;
        std::string a0 = core_s;     argv.push_back(a0.data());
        std::string a1 = "-d";       argv.push_back(a1.data());
        std::string a2 = dir_s;      argv.push_back(a2.data());
        std::string a3 = "-f";       argv.push_back(a3.data());
        std::string a4 = file_s;     argv.push_back(a4.data());
        argv.push_back(nullptr);
        ::execv(core_s.c_str(), argv.data());
        const char* msg = "exec failed\n";
        ::write(STDERR_FILENO, msg, std::strlen(msg));
        ::_exit(127);
    }
    // 父进程
    ::close(pipefd[1]);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stdout_pipe_[0] = pipefd[0];
        stdout_pipe_[1] = -1;
        pid_ = child;
    }
#endif

    StartMonitorThreads();
    return true;
}

void ClashProcess::StartMonitorThreads() {
    // 假定旧 monitor 线程已回收（由 StopImpl 或上一次 ExitMonitor 完成）
    if (exit_thread_.joinable()) exit_thread_.join();
    if (log_thread_.joinable()) log_thread_.join();
    exit_thread_ = std::thread([this] { ExitMonitor(); });
    log_thread_ = std::thread([this] { LogReader(); });
}

void ClashProcess::ExitMonitor() {
#ifdef _WIN32
    HANDLE h = nullptr;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        h = process_handle_;
    }
    if (h == nullptr) return;

    // 阻塞等待进程退出
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    bool got = GetExitCodeProcess(h, &code) != 0;
    int exit_code = got ? static_cast<int>(code) : -1;

    // 关闭父端写端，让日志读取线程收到 EOF
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stdout_write_) { CloseHandle(stdout_write_); stdout_write_ = nullptr; }
    }
    // 等待日志读取线程退出（此时它应已收到 EOF）
    if (log_thread_.joinable()) log_thread_.join();
#else
    pid_t cpid = -1;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cpid = pid_;
    }
    if (cpid <= 0) return;

    int status = 0;
    pid_t w = ::waitpid(cpid, &status, 0);
    int exit_code = -1;
    if (w == cpid) {
        if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
    }
    if (log_thread_.joinable()) log_thread_.join();
#endif

    // 触发 on_exit 回调并决定是否自动重启
    ExitCallback cb;
    bool auto_restart = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = on_exit_;
        if (!user_stopped_ && max_restart_count_ > 0 &&
            restart_count_ < max_restart_count_) {
            auto_restart = true;
        }
        CleanupHandlesLocked();
    }
    if (cb) cb(exit_code);

    // 自动重启在独立 detached 线程中执行，避免与 exit_thread_ 生命周期冲突。
    // stopping_ 会在 Stop/destructor 中置位，TryAutoRestart 会据此放弃重启。
    if (auto_restart) {
        std::thread([this] { TryAutoRestart(); }).detach();
    }
}

void ClashProcess::LogReader() {
    std::string buf; // 行缓冲（未结束的部分行）
    constexpr size_t kChunk = 4096;

#ifdef _WIN32
    HANDLE read_h = nullptr;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        read_h = stdout_read_;
    }
    if (read_h == nullptr) return;

    char chunk[kChunk];
    for (;;) {
        DWORD got = 0;
        BOOL ok = ReadFile(read_h, chunk, kChunk, &got, nullptr);
        if (!ok || got == 0) {
            break; // 管道关闭（EOF）或错误
        }
        buf.append(chunk, got);
        size_t pos = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = buf.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            DispatchLog(line);
            pos = nl + 1;
        }
        buf.erase(0, pos);
    }
#else
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        fd = stdout_pipe_[0];
    }
    if (fd < 0) return;

    char chunk[kChunk];
    for (;;) {
        ssize_t n = ::read(fd, chunk, kChunk);
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
        size_t pos = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = buf.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            DispatchLog(line);
            pos = nl + 1;
        }
        buf.erase(0, pos);
    }
#endif
    // 刷新尾部未换行的内容
    if (!buf.empty()) {
        if (buf.back() == '\r') buf.pop_back();
        DispatchLog(buf);
    }
}

void ClashProcess::TryAutoRestart() {
    // 在独立 detached 线程中运行。使用循环而非递归，避免重复加锁与计数错乱。
    // restart_count_ 在每次循环开始时自增并写回成员，供 ExitMonitor 判断上限；
    // 用户主动 Start/Restart 会将其清零，开启新一轮会话。
    int attempt = 0;
    for (;;) {
        ++attempt;
        fs::path core_path, config_dir, config_file;
        int max_n = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            core_path = core_path_;
            config_dir = config_dir_;
            config_file = config_file_;
            max_n = max_restart_count_;
            restart_count_ = attempt; // 供外部观测与 ExitMonitor 上限判断
        }

        if (attempt > max_n) {
            DispatchLog(std::string("[clash-process] auto-restart giving up after ") +
                        std::to_string(max_n) + " attempts");
            return;
        }

        // 指数退避：1s / 2s / 4s / 8s / 16s ...（睡眠期间响应 Stop）
        int delay_sec = 1 << (attempt - 1);
        for (int i = 0; i < delay_sec * 10; ++i) {
            if (stopping_.load(std::memory_order_acquire)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // 用户在此期间主动停止：放弃自动重启（stopping_ 在 Stop 后保持 true）
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (stopping_.load(std::memory_order_acquire) || user_stopped_) return;
        }

        // 序列化生命周期操作，避免与并发的 Stop/Start 竞争 exit_thread_ join
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> op(op_mutex_);
            // 持锁后再次检查：Stop 可能在睡眠期间被调用
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (stopping_.load(std::memory_order_acquire) || user_stopped_) return;
            }
            ok = StartImpl(core_path, config_dir, config_file, &err);
        }
        if (ok) {
            // 重启成功：重置计数，下次崩溃获得完整重试配额
            {
                std::lock_guard<std::mutex> lk(mutex_);
                restart_count_ = 0;
            }
            DispatchLog(std::string("[clash-process] auto-restart attempt ") +
                        std::to_string(attempt) + " succeeded");
            return;
        }
        DispatchLog(std::string("[clash-process] auto-restart attempt ") +
                    std::to_string(attempt) + " failed: " + err);
        // 失败：循环继续下一次尝试（op_mutex_ 已释放）
    }
}

bool ClashProcess::Stop(std::string* err) {
    std::lock_guard<std::mutex> op(op_mutex_);
    return StopImpl(err);
}

bool ClashProcess::StopImpl(std::string* err) {
    // 持 op_mutex_（不持 mutex_）期间 join exit_thread_ 是安全的：
    // ExitMonitor/LogReader 仅使用 mutex_，不会因 op_mutex_ 阻塞而死锁。
    bool was_running = false;
#ifdef _WIN32
    HANDLE h = nullptr;
#else
    pid_t cpid = -1;
#endif
    {
        std::lock_guard<std::mutex> lk(mutex_);
        was_running = IsRunningLocked();
        // 无论是否运行都设置标志：防止 ExitMonitor 在此期间触发 TryAutoRestart
        user_stopped_ = true;
        stopping_.store(true, std::memory_order_release);
#ifdef _WIN32
        h = process_handle_;
#else
        cpid = pid_;
#endif
    }

#ifdef _WIN32
    if (was_running && h) {
        // mihomo 无优雅关闭 IPC，直接 TerminateProcess（与 Rust 版 child.kill() 一致）
        if (!TerminateProcess(h, 1)) {
            if (err) *err = WinError(GetLastError());
            // 仍继续等待监控线程退出
        }
    }
#else
    if (was_running && cpid > 0) {
        // 优雅终止：SIGTERM，等待 2s 后 SIGKILL。
        // 注意：不在此处 waitpid 回收，由 ExitMonitor 的 waitpid 统一回收，
        // 避免两线程同时 waitpid 导致退出码丢失（ECHILD）。
        ::kill(cpid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (IsRunning()) ::kill(cpid, SIGKILL);
    }
#endif

    // 等待 ExitMonitor 退出（其内部会 join log_thread_）
    if (exit_thread_.joinable()) exit_thread_.join();
    // log_thread_ 由 ExitMonitor join；此处仅兜底（ExitMonitor 未启动等异常场景）
    if (log_thread_.joinable()) log_thread_.join();

    {
        std::lock_guard<std::mutex> lk(mutex_);
        CleanupHandlesLocked();
    }
    // 注意：stopping_ 保持为 true 直到下一次 StartImpl。
    // 这样在 Stop 期间仍在退避睡眠的自动重启线程会看到 stopping_/user_stopped_
    // 并放弃重启，避免「用户停止内核后又被后台线程拉起来」。
    if (!was_running) {
        if (err) *err = "not running";
        return false;
    }
    return true;
}

bool ClashProcess::Restart(std::string* err) {
    std::lock_guard<std::mutex> op(op_mutex_);
    fs::path core_path, config_dir, config_file;
    bool running = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        core_path = core_path_;
        config_dir = config_dir_;
        config_file = config_file_;
        if (core_path.empty()) {
            if (err) *err = "no previous start parameters";
            return false;
        }
        running = IsRunningLocked();
    }

    if (running) {
        std::string stop_err;
        if (!StopImpl(&stop_err)) {
            if (err) *err = "stop failed: " + stop_err;
            return false;
        }
    }
    // 用户主动 Restart：重置自动重启计数
    {
        std::lock_guard<std::mutex> lk(mutex_);
        restart_count_ = 0;
    }
    return StartImpl(core_path, config_dir, config_file, err);
}

// static
fs::path ClashProcess::ResolveCorePath(const std::string& core_name) {
    if (core_name.empty()) return {};
    fs::path res_dir = clash_yuka::utils::app_resources_dir();
    fs::path candidate = res_dir / core_name;
#ifdef _WIN32
    candidate += ".exe";
#endif
    std::error_code ec;
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
        return candidate;
    }
    // 兼容：不带后缀的形式（POSIX 或已带后缀）
    fs::path bare = res_dir / core_name;
    if (fs::exists(bare, ec) && fs::is_regular_file(bare, ec)) {
        return bare;
    }
    return {};
}

} // namespace clash_yuka::clash
