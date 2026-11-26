// ClashYuka 服务模式实现
// 独立可执行文件，通过 Windows 服务控制管理器（SCM）管理 mihomo 内核
//
// 命令行参数：
//   install   - 安装服务（需要管理员权限）
//   uninstall - 卸载服务（需要管理员权限）
//   start     - 启动服务（需要管理员权限）
//   stop      - 停止服务（需要管理员权限）
//   restart   - 重启服务（需要管理员权限）
//   status    - 输出服务状态（JSON 格式）
//   （无参数） - 作为服务主函数运行（由 SCM 调用）
//
// 设计参考 clash-nyanpasu：服务程序本身只负责进程生命周期管理，
// mihomo 的配置和 API 由 GUI 端通过 HTTP 直接控制。

#include <windows.h>
#include <winsvc.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---- 配置常量 ----

static constexpr const wchar_t* kServiceName = L"ClashYuka Service";
static constexpr const wchar_t* kServiceDisplayName = L"ClashYuka Service";
static constexpr const wchar_t* kServiceDescription =
    L"ClashYuka mihomo kernel service";

// ---- 全局状态 ----

static SERVICE_STATUS_HANDLE g_svc_status_handle = nullptr;
static SERVICE_STATUS g_svc_status = {0};
static HANDLE g_stop_event = nullptr;
static PROCESS_INFORMATION g_mihomo_pi = {0};

// ---- 工具函数 ----

static fs::path get_exe_path() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buf);
    }
    return {};
}

static fs::path get_service_dir() {
    return get_exe_path().parent_path();
}

static void log_service(const std::string& msg) {
    try {
        fs::path log_path = get_service_dir() / "service.log";
        std::ofstream ofs(log_path, std::ios::app);
        if (ofs.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char time_buf[64];
            sprintf_s(time_buf, sizeof(time_buf),
                "%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
            ofs << "[" << time_buf << "] " << msg << std::endl;
        }
    } catch (...) {
    }
}

// ---- mihomo 进程管理 ----

static bool start_mihomo() {
    fs::path mihomo_path = get_service_dir() / "mihomo.exe";
    if (!fs::exists(mihomo_path)) {
        log_service("mihomo.exe not found: " + mihomo_path.string());
        return false;
    }

    fs::path work_dir = get_service_dir();
    std::wstring cmd = L"\"" + mihomo_path.wstring() + L"\" -d \"" +
                       work_dir.wstring() + L"\" -f \"" +
                       work_dir.wstring() + L"\\config.yaml\"";

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
        nullptr, work_dir.wstring().c_str(), &si, &g_mihomo_pi);

    if (!ok) {
        DWORD err = GetLastError();
        char err_msg[256];
        sprintf_s(err_msg, sizeof(err_msg),
            "CreateProcess mihomo failed: %lu", err);
        log_service(err_msg);
        return false;
    }

    log_service("mihomo started, pid=" + std::to_string(g_mihomo_pi.dwProcessId));
    return true;
}

static void stop_mihomo() {
    if (g_mihomo_pi.hProcess) {
        TerminateProcess(g_mihomo_pi.hProcess, 0);
        WaitForSingleObject(g_mihomo_pi.hProcess, 5000);
        CloseHandle(g_mihomo_pi.hProcess);
        CloseHandle(g_mihomo_pi.hThread);
        g_mihomo_pi.hProcess = nullptr;
        g_mihomo_pi.hThread = nullptr;
        log_service("mihomo stopped");
    }
}

// ---- 服务控制处理 ----

static VOID WINAPI svc_ctrl_handler(DWORD ctrl) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_svc_status.dwControlsAccepted = 0;
            SetServiceStatus(g_svc_status_handle, &g_svc_status);
            SetEvent(g_stop_event);
            break;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(g_svc_status_handle, &g_svc_status);
            break;

        default:
            break;
    }
}

static VOID WINAPI svc_main(DWORD argc, LPTSTR* argv) {
    g_svc_status_handle = RegisterServiceCtrlHandlerW(
        kServiceName, svc_ctrl_handler);
    if (!g_svc_status_handle) {
        log_service("RegisterServiceCtrlHandler failed");
        return;
    }

    g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState = SERVICE_START_PENDING;
    g_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_svc_status.dwWin32ExitCode = 0;
    g_svc_status.dwServiceSpecificExitCode = 0;
    g_svc_status.dwCheckPoint = 0;
    g_svc_status.dwWaitHint = 3000;
    SetServiceStatus(g_svc_status_handle, &g_svc_status);

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop_event) {
        g_svc_status.dwCurrentState = SERVICE_STOPPED;
        g_svc_status.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_svc_status_handle, &g_svc_status);
        return;
    }

    if (!start_mihomo()) {
        g_svc_status.dwCurrentState = SERVICE_STOPPED;
        g_svc_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_svc_status.dwServiceSpecificExitCode = 1;
        SetServiceStatus(g_svc_status_handle, &g_svc_status);
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
        return;
    }

    g_svc_status.dwCurrentState = SERVICE_RUNNING;
    g_svc_status.dwCheckPoint = 0;
    g_svc_status.dwWaitHint = 0;
    SetServiceStatus(g_svc_status_handle, &g_svc_status);

    while (true) {
        DWORD wait_result = WaitForSingleObject(g_stop_event, 2000);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }

        if (g_mihomo_pi.hProcess) {
            DWORD exit_code;
            if (GetExitCodeProcess(g_mihomo_pi.hProcess, &exit_code) &&
                exit_code != STILL_ACTIVE) {
                log_service("mihomo exited unexpectedly, code=" +
                            std::to_string(exit_code) + ", restarting...");
                CloseHandle(g_mihomo_pi.hProcess);
                CloseHandle(g_mihomo_pi.hThread);
                g_mihomo_pi.hProcess = nullptr;
                g_mihomo_pi.hThread = nullptr;
                std::this_thread::sleep_for(std::chrono::seconds(3));
                start_mihomo();
            }
        }
    }

    g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
    g_svc_status.dwControlsAccepted = 0;
    SetServiceStatus(g_svc_status_handle, &g_svc_status);

    stop_mihomo();

    CloseHandle(g_stop_event);
    g_stop_event = nullptr;

    g_svc_status.dwCurrentState = SERVICE_STOPPED;
    g_svc_status.dwWin32ExitCode = 0;
    SetServiceStatus(g_svc_status_handle, &g_svc_status);
}

// ---- 命令行工具 ----

static SC_HANDLE open_scm(DWORD access = SC_MANAGER_ALL_ACCESS) {
    return OpenSCManagerW(nullptr, nullptr, access);
}

static SC_HANDLE open_service(SC_HANDLE scm, DWORD access = SERVICE_ALL_ACCESS) {
    return OpenServiceW(scm, kServiceName, access);
}

static int cmd_install() {
    fs::path exe_path = get_exe_path();
    if (exe_path.empty()) {
        std::cerr << "Failed to get exe path" << std::endl;
        return 1;
    }

    SC_HANDLE scm = open_scm();
    if (!scm) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return 1;
    }

    SC_HANDLE svc = open_service(scm, SERVICE_QUERY_STATUS);
    if (svc) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        std::cout << "Service already installed" << std::endl;
        return 0;
    }

    svc = CreateServiceW(
        scm,
        kServiceName,
        kServiceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        exe_path.wstring().c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Failed to create service: " << err << std::endl;
        return 1;
    }

    SERVICE_DESCRIPTIONW sd;
    sd.lpDescription = (LPWSTR)kServiceDescription;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &sd);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    std::cout << "Service installed successfully" << std::endl;
    return 0;
}

static int cmd_uninstall() {
    SC_HANDLE scm = open_scm();
    if (!scm) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return 1;
    }

    SC_HANDLE svc = open_service(scm, SERVICE_ALL_ACCESS);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::cout << "Service not installed" << std::endl;
            return 0;
        }
        std::cerr << "Failed to open service: " << err << std::endl;
        return 1;
    }

    SERVICE_STATUS ss;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &ss)) {
        int wait = 0;
        while (QueryServiceStatus(svc, &ss) &&
               ss.dwCurrentState == SERVICE_STOP_PENDING && wait < 30) {
            Sleep(1000);
            wait++;
        }
    }

    if (!DeleteService(svc)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        std::cerr << "Failed to delete service: " << err << std::endl;
        return 1;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    std::cout << "Service uninstalled successfully" << std::endl;
    return 0;
}

static int cmd_start() {
    SC_HANDLE scm = open_scm(GENERIC_READ);
    if (!scm) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return 1;
    }

    SC_HANDLE svc = open_service(scm, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Failed to open service: " << err << std::endl;
        return 1;
    }

    if (!StartServiceW(svc, 0, nullptr)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            std::cout << "Service already running" << std::endl;
            return 0;
        }
        std::cerr << "Failed to start service: " << err << std::endl;
        return 1;
    }

    SERVICE_STATUS ss;
    int wait = 0;
    while (QueryServiceStatus(svc, &ss) &&
           ss.dwCurrentState == SERVICE_START_PENDING && wait < 30) {
        Sleep(1000);
        wait++;
    }

    bool ok = ss.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (ok) {
        std::cout << "Service started successfully" << std::endl;
        return 0;
    } else {
        std::cerr << "Service failed to start" << std::endl;
        return 1;
    }
}

static int cmd_stop() {
    SC_HANDLE scm = open_scm(GENERIC_READ);
    if (!scm) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return 1;
    }

    SC_HANDLE svc = open_service(scm, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::cout << "Service not installed" << std::endl;
            return 0;
        }
        std::cerr << "Failed to open service: " << err << std::endl;
        return 1;
    }

    SERVICE_STATUS ss;
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &ss)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            std::cout << "Service already stopped" << std::endl;
            return 0;
        }
        std::cerr << "Failed to stop service: " << err << std::endl;
        return 1;
    }

    int wait = 0;
    while (QueryServiceStatus(svc, &ss) &&
           ss.dwCurrentState == SERVICE_STOP_PENDING && wait < 30) {
        Sleep(1000);
        wait++;
    }

    bool ok = ss.dwCurrentState == SERVICE_STOPPED;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (ok) {
        std::cout << "Service stopped successfully" << std::endl;
        return 0;
    } else {
        std::cerr << "Service failed to stop" << std::endl;
        return 1;
    }
}

static int cmd_restart() {
    cmd_stop();
    return cmd_start();
}

static int cmd_status() {
    std::string status_str = "not_installed";

    SC_HANDLE scm = open_scm(GENERIC_READ);
    if (scm) {
        SC_HANDLE svc = open_service(scm, SERVICE_QUERY_STATUS);
        if (svc) {
            SERVICE_STATUS ss;
            if (QueryServiceStatus(svc, &ss)) {
                switch (ss.dwCurrentState) {
                    case SERVICE_RUNNING:
                        status_str = "running";
                        break;
                    case SERVICE_STOPPED:
                        status_str = "stopped";
                        break;
                    case SERVICE_START_PENDING:
                        status_str = "starting";
                        break;
                    case SERVICE_STOP_PENDING:
                        status_str = "stopping";
                        break;
                    default:
                        status_str = "unknown";
                        break;
                }
            }
            CloseServiceHandle(svc);
        } else {
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
                status_str = "not_installed";
            }
        }
        CloseServiceHandle(scm);
    }

    std::cout << "{\"status\":\"" << status_str << "\"}" << std::endl;
    return 0;
}

// ---- 主入口 ----

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2) {
        std::wstring cmd = argv[1];

        if (cmd == L"install")   return cmd_install();
        if (cmd == L"uninstall") return cmd_uninstall();
        if (cmd == L"start")     return cmd_start();
        if (cmd == L"stop")      return cmd_stop();
        if (cmd == L"restart")   return cmd_restart();
        if (cmd == L"status")    return cmd_status();
    }

    SERVICE_TABLE_ENTRYW dispatch_table[] = {
        { (LPWSTR)kServiceName, (LPSERVICE_MAIN_FUNCTIONW)svc_main },
        { nullptr, nullptr }
    };

    if (StartServiceCtrlDispatcherW(dispatch_table)) {
        return 0;
    }

    DWORD err = GetLastError();
    if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        std::cout << "Running in console debug mode..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;

        log_service("Starting in console debug mode");

        if (!start_mihomo()) {
            std::cerr << "Failed to start mihomo" << std::endl;
            return 1;
        }

        while (true) {
            Sleep(1000);
            if (g_mihomo_pi.hProcess) {
                DWORD exit_code;
                if (GetExitCodeProcess(g_mihomo_pi.hProcess, &exit_code) &&
                    exit_code != STILL_ACTIVE) {
                    std::cout << "mihomo exited, code=" << exit_code << std::endl;
                    break;
                }
            }
        }

        stop_mihomo();
        return 0;
    }

    std::cerr << "StartServiceCtrlDispatcher failed: " << err << std::endl;
    return 1;
}
