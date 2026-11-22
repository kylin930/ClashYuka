#include "clashyuka/platform/process_backend.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

namespace clashyuka::platform {
namespace {

constexpr DWORD kStopTimeoutMs = 5000;

std::string QuoteArgument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) {
        return argument;
    }

    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string BuildCommandLine(const ProcessSpawnOptions& options) {
    std::string command = QuoteArgument(options.executablePath);
    for (const auto& argument : options.arguments) {
        command += ' ';
        command += QuoteArgument(argument);
    }
    return command;
}

void PumpLines(std::string& pending, std::string_view data,
               const ProcessBackend::LineHandler& handler) {
    if (!handler) {
        return;
    }
    pending.append(data);
    std::size_t start = 0;
    while (true) {
        const std::size_t end = pending.find('\n', start);
        if (end == std::string::npos) {
            break;
        }
        std::string line = pending.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        handler(line);
        start = end + 1;
    }
    pending.erase(0, start);
}

void ReadPipe(HANDLE pipe, ProcessBackend::LineHandler handler) {
    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (::ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) != FALSE && read > 0) {
        PumpLines(pending, std::string_view(buffer, read), handler);
    }
    if (!pending.empty() && handler) {
        handler(pending);
    }
}

void CloseHandleSafely(HANDLE& handle) {
    if (handle != nullptr) {
        ::CloseHandle(handle);
        handle = nullptr;
    }
}

bool CreatePipePair(HANDLE& readHandle, HANDLE& writeHandle) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    if (::CreatePipe(&readHandle, &writeHandle, &attributes, 0) == FALSE) {
        return false;
    }
    if (::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0) == FALSE) {
        CloseHandleSafely(readHandle);
        CloseHandleSafely(writeHandle);
        return false;
    }
    return true;
}

// Console control events only reach processes attached to the same console.
// This is intentionally limited to GUI processes without a console, so a
// CTRL_BREAK can never leak into the user's terminal.
bool RequestConsoleBreak(DWORD pid) {
    if (::GetConsoleWindow() != nullptr) {
        return false;
    }
    if (::AttachConsole(pid) == FALSE) {
        return false;
    }
    ::SetConsoleCtrlHandler(nullptr, TRUE);
    const BOOL sent = ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
    ::FreeConsole();
    return sent != FALSE;
}

class WindowsProcessBackend final : public ProcessBackend {
public:
    ~WindowsProcessBackend() override { Stop(); }

    bool Start(const ProcessSpawnOptions& options, Handlers handlers) override {
        Stop();

        if (options.executablePath.empty()) {
            return false;
        }

        HANDLE stdoutRead = nullptr;
        HANDLE stdoutWrite = nullptr;
        HANDLE stderrRead = nullptr;
        HANDLE stderrWrite = nullptr;
        if (!CreatePipePair(stdoutRead, stdoutWrite) || !CreatePipePair(stderrRead, stderrWrite)) {
            CloseHandleSafely(stdoutRead);
            CloseHandleSafely(stdoutWrite);
            CloseHandleSafely(stderrRead);
            CloseHandleSafely(stderrWrite);
            return false;
        }

        HANDLE nullInput = ::CreateFileA(
            "NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
            nullptr);

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nullInput != INVALID_HANDLE_VALUE ? nullInput : nullptr;
        startup.hStdOutput = stdoutWrite;
        startup.hStdError = stderrWrite;

        std::string command = BuildCommandLine(options);
        PROCESS_INFORMATION processInfo{};
        const BOOL created = ::CreateProcessA(
            nullptr, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr,
            &startup, &processInfo);

        CloseHandleSafely(stdoutWrite);
        CloseHandleSafely(stderrWrite);
        if (nullInput != INVALID_HANDLE_VALUE) {
            CloseHandleSafely(nullInput);
        }

        if (created == FALSE) {
            CloseHandleSafely(stdoutRead);
            CloseHandleSafely(stderrRead);
            return false;
        }

        process_ = processInfo.hProcess;
        CloseHandleSafely(processInfo.hThread);
        pid_ = processInfo.dwProcessId;
        {
            std::lock_guard<std::mutex> lock(exitMutex_);
            exitCode_.reset();
        }
        running_.store(true);

        const auto stdoutHandler = handlers.onStdoutLine;
        const auto stderrHandler = handlers.onStderrLine;
        const auto exitHandler = handlers.onExited;

        readers_.emplace_back([stdoutRead, stdoutHandler] { ReadPipe(stdoutRead, stdoutHandler); });
        readers_.emplace_back([stderrRead, stderrHandler] { ReadPipe(stderrRead, stderrHandler); });
        stdoutRead_ = stdoutRead;
        stderrRead_ = stderrRead;

        monitor_ = std::thread([this, exitHandler = std::move(exitHandler)] {
            ::WaitForSingleObject(process_, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(process_, &code);
            {
                std::lock_guard<std::mutex> lock(exitMutex_);
                exitCode_ = static_cast<int>(code);
            }
            running_.store(false);
            if (exitHandler) {
                exitHandler(static_cast<int>(code));
            }
        });

        return true;
    }

    void Stop() override {
        if (process_ == nullptr) {
            return;
        }

        if (running_.load()) {
            // Ask the kernel to exit cooperatively first. Only a GUI process
            // without a console attempts this, so the event cannot affect the
            // user's terminal or unrelated processes.
            if (RequestConsoleBreak(pid_)) {
                ::WaitForSingleObject(process_, 3000);
            }
            if (running_.load()) {
                ::TerminateProcess(process_, 1);
            }
        }
        ::WaitForSingleObject(process_, kStopTimeoutMs);
        running_.store(false);

        JoinThreads();
        CloseHandleSafely(stdoutRead_);
        CloseHandleSafely(stderrRead_);
        CloseHandleSafely(process_);
        pid_ = 0;
    }

    bool IsRunning() const override {
        return process_ != nullptr && running_.load() &&
               ::WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }

    std::uint64_t Pid() const override { return pid_; }

    std::optional<int> ExitCode() const override {
        std::lock_guard<std::mutex> lock(exitMutex_);
        return exitCode_;
    }

private:
    void JoinThreads() {
        if (monitor_.joinable()) {
            monitor_.join();
        }
        for (auto& reader : readers_) {
            if (reader.joinable()) {
                reader.join();
            }
        }
        readers_.clear();
    }

    HANDLE process_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    HANDLE stderrRead_ = nullptr;
    DWORD pid_ = 0;
    std::atomic<bool> running_{false};
    std::vector<std::thread> readers_;
    std::thread monitor_;
    mutable std::mutex exitMutex_;
    std::optional<int> exitCode_;
};

}  // namespace

std::unique_ptr<ProcessBackend> CreateProcessBackend() {
    return std::make_unique<WindowsProcessBackend>();
}

}  // namespace clashyuka::platform
