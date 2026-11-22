#include "clashyuka/platform/process_backend.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace clashyuka::platform {
namespace {

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

void ReadFd(int fd, ProcessBackend::LineHandler handler) {
    std::string pending;
    char buffer[4096];
    ssize_t count = 0;
    while ((count = ::read(fd, buffer, sizeof(buffer))) > 0) {
        PumpLines(pending, std::string_view(buffer, static_cast<std::size_t>(count)), handler);
    }
    if (!pending.empty() && handler) {
        handler(pending);
    }
}

class PosixProcessBackend final : public ProcessBackend {
public:
    ~PosixProcessBackend() override { Stop(); }

    bool Start(const ProcessSpawnOptions& options, Handlers handlers) override {
        Stop();

        if (options.executablePath.empty()) {
            return false;
        }

        int stdoutPipe[2] = {-1, -1};
        int stderrPipe[2] = {-1, -1};
        if (::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0) {
            CloseFd(stdoutPipe[0]);
            CloseFd(stdoutPipe[1]);
            CloseFd(stderrPipe[0]);
            CloseFd(stderrPipe[1]);
            return false;
        }

        const pid_t child = ::fork();
        if (child < 0) {
            CloseFd(stdoutPipe[0]);
            CloseFd(stdoutPipe[1]);
            CloseFd(stderrPipe[0]);
            CloseFd(stderrPipe[1]);
            return false;
        }

        if (child == 0) {
            ::dup2(stdoutPipe[1], STDOUT_FILENO);
            ::dup2(stderrPipe[1], STDERR_FILENO);
            ::close(stdoutPipe[0]);
            ::close(stdoutPipe[1]);
            ::close(stderrPipe[0]);
            ::close(stderrPipe[1]);

            std::vector<char*> argv;
            argv.reserve(options.arguments.size() + 2);
            argv.push_back(const_cast<char*>(options.executablePath.c_str()));
            for (const auto& argument : options.arguments) {
                argv.push_back(const_cast<char*>(argument.c_str()));
            }
            argv.push_back(nullptr);

            ::execvp(argv[0], argv.data());
            ::_exit(127);
        }

        ::close(stdoutPipe[1]);
        ::close(stderrPipe[1]);

        pid_ = child;
        stdoutFd_ = stdoutPipe[0];
        stderrFd_ = stderrPipe[0];
        {
            std::lock_guard<std::mutex> lock(exitMutex_);
            exitCode_.reset();
        }
        running_.store(true);

        const auto stdoutHandler = handlers.onStdoutLine;
        const auto stderrHandler = handlers.onStderrLine;
        const auto exitHandler = handlers.onExited;

        readers_.emplace_back([fd = stdoutFd_, stdoutHandler] { ReadFd(fd, stdoutHandler); });
        readers_.emplace_back([fd = stderrFd_, stderrHandler] { ReadFd(fd, stderrHandler); });

        monitor_ = std::thread([this, exitHandler = std::move(exitHandler)] {
            int status = 0;
            ::waitpid(pid_, &status, 0);
            int code = 0;
            if (WIFEXITED(status)) {
                code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                code = 128 + WTERMSIG(status);
            }
            {
                std::lock_guard<std::mutex> lock(exitMutex_);
                exitCode_ = code;
            }
            running_.store(false);
            if (exitHandler) {
                exitHandler(code);
            }
        });

        return true;
    }

    void Stop() override {
        if (pid_ <= 0) {
            return;
        }

        if (running_.load()) {
            ::kill(pid_, SIGTERM);
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (running_.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (running_.load()) {
                ::kill(pid_, SIGKILL);
            }
        }

        JoinThreads();
        CloseFd(stdoutFd_);
        CloseFd(stderrFd_);
        pid_ = -1;
    }

    bool IsRunning() const override { return running_.load(); }

    std::uint64_t Pid() const override { return pid_ > 0 ? static_cast<std::uint64_t>(pid_) : 0; }

    std::optional<int> ExitCode() const override {
        std::lock_guard<std::mutex> lock(exitMutex_);
        return exitCode_;
    }

private:
    static void CloseFd(int& fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

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

    pid_t pid_ = -1;
    int stdoutFd_ = -1;
    int stderrFd_ = -1;
    std::atomic<bool> running_{false};
    std::vector<std::thread> readers_;
    std::thread monitor_;
    mutable std::mutex exitMutex_;
    std::optional<int> exitCode_;
};

}  // namespace

std::unique_ptr<ProcessBackend> CreateProcessBackend() {
    return std::make_unique<PosixProcessBackend>();
}

}  // namespace clashyuka::platform
