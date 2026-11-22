#include "clashyuka/mihomo/process.h"

#include <utility>

#include "clashyuka/platform/process_backend.h"

namespace clashyuka::mihomo {

struct MihomoProcess::Impl {
    bool Start(const MihomoLaunchOptions& launchOptions, MihomoProcessHandlers processHandlers) {
        if (launchOptions.executablePath.empty()) {
            return false;
        }
        if (IsRunning()) {
            return false;
        }

        backend = platform::CreateProcessBackend();

        platform::ProcessSpawnOptions spawnOptions;
        spawnOptions.executablePath = launchOptions.executablePath;
        spawnOptions.arguments = launchOptions.arguments;

        platform::ProcessBackend::Handlers backendHandlers;
        backendHandlers.onStdoutLine = std::move(processHandlers.onStdoutLine);
        backendHandlers.onStderrLine = std::move(processHandlers.onStderrLine);
        backendHandlers.onExited = std::move(processHandlers.onExited);

        if (!backend->Start(spawnOptions, std::move(backendHandlers))) {
            backend.reset();
            return false;
        }
        return true;
    }

    void Stop() {
        if (backend) {
            backend->Stop();
        }
    }

    bool IsRunning() const { return backend != nullptr && backend->IsRunning(); }

    std::unique_ptr<platform::ProcessBackend> backend;
};

MihomoProcess::MihomoProcess() : impl_(std::make_unique<Impl>()) {}

MihomoProcess::~MihomoProcess() = default;

bool MihomoProcess::Start(const MihomoLaunchOptions& options, MihomoProcessHandlers handlers) {
    return impl_->Start(options, std::move(handlers));
}

bool MihomoProcess::Restart(const MihomoLaunchOptions& options, MihomoProcessHandlers handlers) {
    impl_->Stop();
    impl_->backend.reset();
    return impl_->Start(options, std::move(handlers));
}

void MihomoProcess::Stop() {
    impl_->Stop();
}

bool MihomoProcess::IsRunning() const {
    return impl_->IsRunning();
}

std::optional<std::uint64_t> MihomoProcess::GetPid() const {
    if (impl_->backend == nullptr) {
        return std::nullopt;
    }
    const std::uint64_t pid = impl_->backend->Pid();
    if (pid == 0) {
        return std::nullopt;
    }
    return pid;
}

std::optional<int> MihomoProcess::GetExitCode() const {
    if (impl_->backend == nullptr) {
        return std::nullopt;
    }
    return impl_->backend->ExitCode();
}

}  // namespace clashyuka::mihomo
