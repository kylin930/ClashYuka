#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clashyuka::platform {

struct ProcessSpawnOptions {
    std::string executablePath;
    std::vector<std::string> arguments;
};

// Platform process backend. Implementations live in src/platform/{windows,posix}
// so the shared Mihomo modules never call CreateProcess/fork directly.
//
// Callbacks are invoked from background threads; callers must marshal them to
// the GUI thread before touching wxWidgets objects.
class ProcessBackend {
public:
    using LineHandler = std::function<void(std::string_view line)>;
    using ExitHandler = std::function<void(int exitCode)>;

    struct Handlers {
        LineHandler onStdoutLine;
        LineHandler onStderrLine;
        ExitHandler onExited;
    };

    virtual ~ProcessBackend() = default;

    virtual bool Start(const ProcessSpawnOptions& options, Handlers handlers) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual std::uint64_t Pid() const = 0;
    virtual std::optional<int> ExitCode() const = 0;
};

std::unique_ptr<ProcessBackend> CreateProcessBackend();

}  // namespace clashyuka::platform
