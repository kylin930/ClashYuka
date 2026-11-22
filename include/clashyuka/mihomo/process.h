#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clashyuka::mihomo {

struct MihomoLaunchOptions {
    // Absolute path to the mihomo executable. Never downloaded automatically.
    std::string executablePath;
    // Extra arguments, for example {"-f", "config.yaml", "-d", "home"}.
    std::vector<std::string> arguments;
};

struct MihomoProcessHandlers {
    // Called from worker threads; marshal to the GUI thread before touching widgets.
    std::function<void(std::string_view line)> onStdoutLine;
    std::function<void(std::string_view line)> onStderrLine;
    // Called from the monitor thread when the process terminates.
    std::function<void(int exitCode)> onExited;
};

// Manages a single external mihomo process.
//
// Launching/waiting is implemented in the platform layer (wxProcess was
// evaluated but its exit notification depends on the GUI event loop, which
// makes headless verification and shutdown ordering unreliable). The shared
// code below never calls CreateProcess/fork directly.
// All callbacks run on background threads; marshal to the GUI thread before
// touching wxWidgets objects. Start/Restart/Stop may be called from the GUI
// thread; the destructor stops the child and joins all worker threads.
class MihomoProcess {
public:
    MihomoProcess();
    ~MihomoProcess();

    MihomoProcess(const MihomoProcess&) = delete;
    MihomoProcess& operator=(const MihomoProcess&) = delete;

    // Returns false when the executable path is empty, the process is already
    // running, or wxExecute failed.
    bool Start(const MihomoLaunchOptions& options, MihomoProcessHandlers handlers = {});
    bool Restart(const MihomoLaunchOptions& options, MihomoProcessHandlers handlers = {});

    // Idempotent. Sends a terminate request first, then kills the process if it
    // is still alive.
    void Stop();

    bool IsRunning() const;
    std::optional<std::uint64_t> GetPid() const;
    std::optional<int> GetExitCode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clashyuka::mihomo
