#include "service_manager.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "core/utils/dirs.h"

namespace clash_yuka::core {

namespace fs = std::filesystem;

// RunElevated 返回值约定：
//   -1  service exe 不存在，或 ShellExecuteExW 调用失败（非用户取消）
//   -2  用户取消了 UAC 提示（ERROR_CANCELLED）
//   >=0 提权进程的退出码（0 表示成功）
constexpr int kErrElevateFailed = -1;
constexpr int kErrUserCancelled = -2;

namespace {
void log_sm(const std::string& msg) {
    // 简单的调试日志，写到 resources/service-manager.log
    try {
        fs::path log_path = utils::app_resources_dir() / "service-manager.log";
        fs::path parent = log_path.parent_path();
        std::error_code ec;
        fs::create_directories(parent, ec);
        std::ofstream ofs(log_path, std::ios::app);
        if (ofs.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char buf[64];
            sprintf_s(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
            ofs << "[" << buf << "] " << msg << std::endl;
        }
    } catch (...) {
    }
}
} // namespace

ServiceManager& ServiceManager::Get() {
    static ServiceManager instance;
    return instance;
}

fs::path ServiceManager::service_exe() const {
    return utils::app_resources_dir() / "clashyuka-service.exe";
}

std::string ServiceManager::RunStatus() const {
    fs::path exe = service_exe();
    if (!fs::exists(exe)) return {};

    std::wstring cmd = L"\"" + exe.wstring() + L"\" status";

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE h_read = nullptr;
    HANDLE h_write = nullptr;
    if (!CreatePipe(&h_read, &h_write, &sa, 0)) {
        return {};
    }
    SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = h_write;
    si.hStdError = h_write;

    PROCESS_INFORMATION pi = {0};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(h_write);

    if (!ok) {
        CloseHandle(h_read);
        return {};
    }

    std::string output;
    char buf[4096];
    DWORD bytes_read = 0;
    while (ReadFile(h_read, buf, sizeof(buf) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h_read);

    return output;
}

ServiceState ServiceManager::QueryStatus() const {
    ServiceState state;
    std::string json = RunStatus();
    if (json.empty()) {
        state.status = ServiceStatus::Unknown;
        state.error = "service exe not found or failed to run";
        return state;
    }

    // 简单解析 JSON：找 "status":"xxx"
    size_t pos = json.find("\"status\":\"");
    if (pos == std::string::npos) {
        state.status = ServiceStatus::Unknown;
        state.error = "invalid status output";
        return state;
    }
    pos += 10;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) {
        state.status = ServiceStatus::Unknown;
        return state;
    }
    std::string val = json.substr(pos, end - pos);

    if (val == "not_installed") state.status = ServiceStatus::NotInstalled;
    else if (val == "running") state.status = ServiceStatus::Running;
    else if (val == "stopped") state.status = ServiceStatus::Stopped;
    else if (val == "starting") state.status = ServiceStatus::Starting;
    else if (val == "stopping") state.status = ServiceStatus::Stopping;
    else state.status = ServiceStatus::Unknown;

    return state;
}

int ServiceManager::RunElevated(const std::wstring& args) const {
    fs::path exe = service_exe();
    if (!fs::exists(exe)) {
        log_sm("RunElevated: service exe not found: " + exe.string());
        return kErrElevateFailed;
    }

    // 将 wstring args 转为 string 用于日志（ASCII 命令足够）
    std::string args_str(args.size(), '\0');
    for (size_t i = 0; i < args.size(); ++i) {
        args_str[i] = static_cast<char>(args[i]);
    }
    log_sm("RunElevated: invoking '" + args_str + "' on " + exe.string());

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    // 必须使用 SW_SHOWNORMAL（参考 clash-nyanpasu: .gui(true).show(true)）。
    // 之前用 SW_HIDE 会隐藏 UAC 提示窗口，导致：
    //   1. 用户看不到 UAC 弹窗，无法确认
    //   2. ShellExecuteExW 直接失败返回 ERROR_CANCELLED
    //   3. RunElevated 返回 -1，UI 误以为安装失败
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        log_sm(std::string("RunElevated: ShellExecuteExW failed, GLE=") +
               std::to_string(err));
        if (err == ERROR_CANCELLED) {
            // 用户在 UAC 弹窗点了"否"
            return kErrUserCancelled;
        }
        return kErrElevateFailed;
    }

    if (!sei.hProcess) {
        log_sm("RunElevated: sei.hProcess is null without error");
        return kErrElevateFailed;
    }

    // 等待提权进程结束（最多 60 秒）
    DWORD wait = WaitForSingleObject(sei.hProcess, 60000);
    DWORD exit_code = 0;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);

    if (wait == WAIT_TIMEOUT) {
        log_sm("RunElevated: timed out waiting for elevated process");
    } else {
        log_sm(std::string("RunElevated: process exited with code ") +
               std::to_string(exit_code));
    }

    return static_cast<int>(exit_code);
}

bool ServiceManager::Install() const {
    // 先把服务 exe 复制到 resources 目录（如果还没有）
    fs::path exe = service_exe();
    if (!fs::exists(exe)) {
        last_error_ = "服务程序文件不存在: " + exe.string();
        log_sm(last_error_);
        return false;
    }
    int code = RunElevated(L"install");
    last_error_ = InterpretElevateCode(code);
    return code == 0;
}

bool ServiceManager::Uninstall() const {
    int code = RunElevated(L"uninstall");
    last_error_ = InterpretElevateCode(code);
    return code == 0;
}

bool ServiceManager::Start() const {
    int code = RunElevated(L"start");
    last_error_ = InterpretElevateCode(code);
    return code == 0;
}

bool ServiceManager::Stop() const {
    int code = RunElevated(L"stop");
    last_error_ = InterpretElevateCode(code);
    return code == 0;
}

bool ServiceManager::Restart() const {
    int code = RunElevated(L"restart");
    last_error_ = InterpretElevateCode(code);
    return code == 0;
}

std::string ServiceManager::last_error() const {
    return last_error_;
}

std::string ServiceManager::InterpretElevateCode(int code) const {
    switch (code) {
        case 0: return {};
        case kErrElevateFailed:
            return "提权调用失败（ShellExecuteExW 返回 FALSE）";
        case kErrUserCancelled:
            return "用户取消了 UAC 提示";
        default:
            return std::string("服务程序退出码: ") + std::to_string(code);
    }
}

bool ServiceManager::IsInstalled() const {
    auto state = QueryStatus();
    return state.status != ServiceStatus::NotInstalled &&
           state.status != ServiceStatus::Unknown;
}

bool ServiceManager::IsRunning() const {
    auto state = QueryStatus();
    return state.status == ServiceStatus::Running;
}

} // namespace clash_yuka::core
