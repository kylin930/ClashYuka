#include "clashyuka/runtime/kernel_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string_view>
#include <utility>

#include "clashyuka/core/timing.h"
#include "clashyuka/runtime/runtime_config.h"

namespace clashyuka::runtime {
namespace {

namespace fs = std::filesystem;

constexpr int kShutdownWaitMs = 15000;
constexpr std::size_t kRecentOutputLimit = 8;

fs::path PathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string FormatAddress(const std::string& host, int port) {
    if (host.find(':') != std::string::npos && !host.starts_with('[')) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

}  // namespace

struct KernelSession::SharedState {
    mutable std::mutex mutex;
    KernelPhase phase = KernelPhase::Stopped;
    std::uint64_t generation = 0;
    std::string version;
    std::string apiAddress;
    std::string configPath;
    bool generatedConfig = false;
    int mixedPort = 0;
    core::Error error;
    std::string eventError;
    KernelSessionObserver* observer = nullptr;
    mihomo::MihomoApiConfig apiConfig;
    std::string runningProfileId;
    std::optional<ProfileSwitchResult> pendingSwitch;
    std::optional<ProfileSwitchResult> switchResult;
    std::uint64_t nextSwitchId = 0;
    std::uint64_t nextPublication = 0;
    std::uint64_t deliveredPublication = 0;
    bool imported = false;
};

struct KernelSession::LaunchConfig {
    KernelOptions options;
    std::unique_ptr<RuntimeConfigCopy> copy;
    bool imported = false;
};

struct KernelSession::ProcessStatus {
    std::atomic<bool> active{true};
    std::atomic<bool> exited{false};
    std::atomic<int> exitCode{0};
};

const char* KernelPhaseName(KernelPhase phase) {
    switch (phase) {
        case KernelPhase::Stopped:
            return "Stopped";
        case KernelPhase::Starting:
            return "Starting";
        case KernelPhase::WaitingForApi:
            return "WaitingForApi";
        case KernelPhase::Running:
            return "Running";
        case KernelPhase::Stopping:
            return "Stopping";
        case KernelPhase::Failed:
            return "Failed";
    }
    return "Unknown";
}

KernelSession::KernelSession(std::unique_ptr<KernelDriverFactory> drivers, Dispatcher dispatcher)
    : drivers_(std::move(drivers)),
      dispatcher_(std::move(dispatcher)),
      state_(std::make_shared<SharedState>()) {
    worker_ = std::thread([this] { WorkerMain(); });
}

KernelSession::~KernelSession() {
    Shutdown();
}

void KernelSession::SetObserver(KernelSessionObserver* observer) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->observer = observer;
    }
    if (observer != nullptr) {
        Publish();
    }
}

void KernelSession::SetOptions(KernelOptions options) {
    std::lock_guard<std::mutex> lock(optionsMutex_);
    options_ = std::move(options);
}

KernelOptions KernelSession::Options() const {
    std::lock_guard<std::mutex> lock(optionsMutex_);
    return options_;
}

KernelPhase KernelSession::Phase() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->phase;
}

KernelSnapshot KernelSession::Snapshot() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    KernelSnapshot snapshot;
    snapshot.phase = state_->phase;
    snapshot.generation = state_->generation;
    snapshot.version = state_->version;
    snapshot.apiAddress = state_->apiAddress;
    snapshot.configPath = state_->configPath;
    snapshot.generatedConfig = state_->generatedConfig;
    snapshot.mixedPort = state_->mixedPort;
    snapshot.error = state_->error;
    snapshot.eventError = state_->eventError;
    snapshot.runningProfileId = state_->runningProfileId;
    snapshot.profileSwitchInProgress = state_->pendingSwitch.has_value();
    snapshot.profileSwitchResult = state_->switchResult;
    return snapshot;
}

mihomo::MihomoApiConfig KernelSession::ApiConfig() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->apiConfig;
}

void KernelSession::Start() {
    if (shutdownRequested_.load()) {
        return;
    }
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        const KernelPhase phase = state_->phase;
        if (state_->pendingSwitch || phase == KernelPhase::Starting ||
            phase == KernelPhase::WaitingForApi || phase == KernelPhase::Running ||
            phase == KernelPhase::Stopping) {
            return;
        }
        generation = ++state_->generation;
        state_->phase = KernelPhase::Starting;
        state_->version.clear();
        state_->error = {};
        state_->eventError.clear();
        state_->configPath.clear();
        state_->generatedConfig = false;
        cancelRequested_.store(false);
    }
    Publish();
    Enqueue({CommandType::Start, generation});
}

void KernelSession::Stop() {
    if (shutdownRequested_.load()) {
        return;
    }
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        const KernelPhase phase = state_->phase;
        if (phase == KernelPhase::Stopped || phase == KernelPhase::Stopping ||
            phase == KernelPhase::Failed) {
            return;
        }
        state_->phase = KernelPhase::Stopping;
        generation = state_->generation;
        cancelRequested_.store(true);
    }
    Publish();
    Enqueue({CommandType::Stop, generation});
}

void KernelSession::Restart() {
    if (shutdownRequested_.load()) {
        return;
    }
    std::uint64_t generation = 0;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->pendingSwitch) {
            return;
        }
        const KernelPhase phase = state_->phase;
        active = phase == KernelPhase::Starting || phase == KernelPhase::WaitingForApi ||
                 phase == KernelPhase::Running || phase == KernelPhase::Stopping;
        if (active) {
            state_->phase = KernelPhase::Stopping;
            generation = state_->generation;
        } else {
            generation = ++state_->generation;
            state_->phase = KernelPhase::Starting;
            state_->version.clear();
            state_->error = {};
            state_->eventError.clear();
            state_->configPath.clear();
            state_->generatedConfig = false;
        }
        cancelRequested_.store(active);
    }
    Publish();
    Enqueue({active ? CommandType::Restart : CommandType::Start, generation});
}

std::optional<std::uint64_t> KernelSession::SwitchProfile(KernelProfile profile) {
    Command command;
    command.type = CommandType::SwitchProfile;
    command.options = Options();
    command.options.profile = std::move(profile);
    std::uint64_t requestId = 0;
    {
        std::scoped_lock lock(state_->mutex, queueMutex_);
        if (shutdownRequested_.load() || state_->phase != KernelPhase::Running ||
            state_->pendingSwitch) {
            return std::nullopt;
        }
        command.generation = state_->generation;
        requestId = ++state_->nextSwitchId;
        command.profileSwitch.requestId = requestId;
        command.profileSwitch.oldProfileId = state_->runningProfileId;
        command.profileSwitch.newProfileId = command.options.profile.id;
        state_->pendingSwitch = command.profileSwitch;
        queue_.push_back(std::move(command));
    }
    Publish();
    queueCv_.notify_all();
    return requestId;
}

void KernelSession::Shutdown() {
    {
        std::scoped_lock lock(state_->mutex, queueMutex_);
        shutdownRequested_.store(true);
        state_->observer = nullptr;
        queue_.clear();
    }
    cancelRequested_.store(true);
    queueCv_.notify_all();

    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        exitCv_.wait_for(lock, std::chrono::milliseconds(kShutdownWaitMs),
                         [this] { return workerExited_; });
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void KernelSession::Enqueue(Command command) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (shutdownRequested_.load()) {
            return;
        }
        queue_.push_back(std::move(command));
    }
    queueCv_.notify_all();
}

void KernelSession::WorkerMain() {
    for (;;) {
        Command command;
        bool shutdown = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return shutdownRequested_.load() || !queue_.empty();
            });
            if (shutdownRequested_.load()) {
                queue_.clear();
                shutdown = true;
            } else {
                command = std::move(queue_.front());
                queue_.pop_front();
            }
        }

        if (shutdown) {
            HandleShutdown();
            break;
        }

        try {
            switch (command.type) {
                case CommandType::Start:
                    HandleStart(command.generation);
                    break;
                case CommandType::Stop:
                    HandleStop(command.generation);
                    break;
                case CommandType::Restart:
                    HandleRestart(command.generation);
                    break;
                case CommandType::SwitchProfile:
                    HandleProfileSwitch(std::move(command));
                    break;
                case CommandType::ProcessExited:
                    HandleProcessExited(command.generation);
                    break;
            }
        } catch (...) {
            std::fprintf(stderr, "[clashyuka] kernel worker error: unknown exception\n");
            Fail(command.generation, core::ErrorKind::Internal,
                 "unexpected error in the kernel worker");
        }
        if (shutdownRequested_.load()) {
            HandleShutdown();
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        workerExited_ = true;
    }
    exitCv_.notify_all();
}

void KernelSession::HandleShutdown() {
    StopEvents();
    StopProcess();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->phase = KernelPhase::Stopped;
        state_->version.clear();
        state_->error = {};
        state_->eventError.clear();
        state_->runningProfileId.clear();
        state_->pendingSwitch.reset();
    }
    activeConfig_.reset();
}

void KernelSession::HandleStart(std::uint64_t generation) {
    if (!IsCurrent(generation)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->phase != KernelPhase::Starting) {
            return;
        }
    }

    auto config = PrepareLaunch(Options(), false);
    if (!config) {
        Fail(generation, config.error().kind, config.error().message);
        return;
    }
    activeConfig_ = std::move(config.value());
    const auto launched = Launch(generation, *activeConfig_);
    if (!launched) {
        Fail(generation, launched.error().kind, launched.error().message);
    }
}

core::Result<std::unique_ptr<KernelSession::LaunchConfig>> KernelSession::PrepareLaunch(
    KernelOptions options, bool switching) {
    const bool imported = options.profile.yaml.has_value();
    if (options.profile.id.empty() || (options.profile.id == "builtin") == imported) {
        return core::MakeError(core::ErrorKind::ConfigFailed, "selected profile content is unavailable");
    }
    std::error_code fileError;
    if (options.executablePath.empty() ||
        !fs::is_regular_file(PathFromUtf8(options.executablePath), fileError) || fileError) {
        return core::MakeError(core::ErrorKind::KernelNotFound, "mihomo executable was not found");
    }
    if (options.workingDirectory.empty() || (!imported && options.configPath.empty())) {
        return core::MakeError(core::ErrorKind::ConfigFailed,
                               "the runtime directory or config path is not configured");
    }
    const RuntimeConfigDefinition definition{options.controllerHost, options.controllerPort,
                                             options.mixedPort, options.secret};
    auto copy = imported ? MakeProfileRuntimeCopy(options.workingDirectory, *options.profile.yaml, definition)
                         : MakeLegacyRuntimeCopy(options.workingDirectory, options.configPath, definition);
    if (!copy) {
        return copy.error();
    }
    options.profile.yaml.reset();
    auto config = std::make_unique<LaunchConfig>(
        LaunchConfig{std::move(options), std::move(copy.value()), imported});
    if (imported || switching) {
        const auto checked = Preflight(*config);
        if (!checked) {
            return checked.error();
        }
    }
    return config;
}

core::Result<void> KernelSession::Preflight(const LaunchConfig& config) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() +
                          milliseconds(std::clamp(config.options.preflightTimeoutMs, 1, 15000));
    auto process = drivers_->CreateProcess();
    if (Cancelled()) {
        return core::MakeError(core::ErrorKind::Cancelled, "profile preflight was cancelled");
    }
    if (!process || !process->Start({config.options.executablePath,
                                    {"-t", "-d", config.copy->workingDirectory,
                                     "-f", config.copy->configPath}}, {})) {
        return core::MakeError(core::ErrorKind::StartFailed, "could not start Mihomo profile preflight");
    }
    while (!Cancelled() && steady_clock::now() < deadline) {
        if (const auto code = process->ExitCode()) {
            process->Stop();
            if (*code == 0) {
                return {};
            }
            return core::MakeError(core::ErrorKind::ProfilePreflightRejected,
                                   "Mihomo rejected the profile configuration");
        }
        InterruptibleSleep(10);
    }
    process->Stop();
    return Cancelled()
        ? core::MakeError(core::ErrorKind::Cancelled, "profile preflight was cancelled")
        : core::MakeError(core::ErrorKind::Timeout, "Mihomo profile preflight timed out");
}

core::Result<void> KernelSession::Launch(std::uint64_t generation, const LaunchConfig& config,
                                        ProfileSwitchResult* readyResult) {
    const auto cancelled = core::MakeError(core::ErrorKind::Cancelled, "kernel launch was cancelled");
    const auto& options = config.options;
    const auto& info = config.copy->info;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (Cancelled() || state_->generation != generation) {
            return cancelled;
        }
        state_->apiAddress = FormatAddress(info.host, info.port);
        state_->configPath = config.copy->configPath;
        state_->generatedConfig = info.generated;
        state_->mixedPort = info.mixedPort;
        state_->imported = config.imported;
    }

    process_ = drivers_->CreateProcess();
    if (!process_) {
        return core::MakeError(core::ErrorKind::StartFailed, "process driver is unavailable");
    }
    const auto status = std::make_shared<ProcessStatus>();
    processStatus_ = status;
    {
        std::lock_guard<std::mutex> lock(outputMutex_);
        recentOutput_.clear();
    }

    mihomo::MihomoApiConfig apiConfig;
    apiConfig.host = info.host;
    apiConfig.port = info.port;
    apiConfig.secret = info.secret;
    apiConfig.connectionTimeoutMs = options.readinessTimeoutMs;
    apiConfig.readTimeoutMs = options.readinessTimeoutMs;
    apiConfig.kernelRunningProbe = [status] { return status->active.load() && !status->exited.load(); };
    api_ = drivers_->CreateApi(apiConfig);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->apiConfig = apiConfig;
    }

    mihomo::MihomoLaunchOptions launch;
    launch.executablePath = options.executablePath;
    launch.arguments = {"-d", config.copy->workingDirectory, "-f", config.copy->configPath};

    mihomo::MihomoProcessHandlers handlers;
    if (!config.imported) {
        handlers.onStdoutLine = [this](std::string_view line) {
            std::lock_guard<std::mutex> lock(outputMutex_);
            if (!line.empty()) {
                recentOutput_.emplace_back(line.substr(0, 300));
                if (recentOutput_.size() > kRecentOutputLimit) {
                    recentOutput_.pop_front();
                }
            }
        };
        handlers.onStderrLine = handlers.onStdoutLine;
    }
    handlers.onExited = [this, generation, status](int exitCode) {
        status->exitCode.store(exitCode);
        status->exited.store(true);
        if (status->active.load() && IsCurrent(generation)) {
            Enqueue({CommandType::ProcessExited, generation});
        }
    };

    if (!process_->Start(launch, std::move(handlers))) {
        return core::MakeError(core::ErrorKind::StartFailed,
            config.imported ? "could not start Mihomo" : "could not start Mihomo. " + RecentOutput());
    }
    core::MarkStartup("mihomo-process-start");

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (Cancelled() || state_->generation != generation) {
            return cancelled;
        }
        state_->phase = KernelPhase::WaitingForApi;
    }
    Publish();

    const int attempts = std::max(1, options.readinessAttempts);
    core::Error lastError;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (Cancelled() || status->exited.load()) {
            break;
        }
        core::Result<mihomo::MihomoVersion> version = api_ ? api_->GetVersion()
                                                           : core::Result<mihomo::MihomoVersion>(
                                                                 core::MakeError(
                                                                     core::ErrorKind::StartFailed,
                                                                     "API driver is unavailable"));
        if (version) {
            if (Cancelled() || status->exited.load() || !process_->IsRunning() || !IsCurrent(generation)) {
                break;
            }

            if (events_ == nullptr) {
                mihomo::MihomoEventConfig eventConfig;
                eventConfig.api = apiConfig;
                events_ = drivers_->CreateEvents(eventConfig);
            }
            if (events_ != nullptr) {
                auto state = state_;
                auto dispatcher = dispatcher_;
                if (options.connectTraffic) {
                    core::Result<void> connected = events_->ConnectTraffic(
                        [state, dispatcher, generation](const mihomo::TrafficStatistics& traffic) {
                            if (!dispatcher) {
                                return;
                            }
                            dispatcher([state, generation, traffic] {
                                KernelSessionObserver* observer = nullptr;
                                {
                                    std::lock_guard<std::mutex> lock(state->mutex);
                                    if (state->generation != generation ||
                                        state->phase != KernelPhase::Running) {
                                        return;
                                    }
                                    observer = state->observer;
                                }
                                if (observer != nullptr) {
                                    observer->OnKernelTraffic(traffic.up, traffic.down);
                                }
                            });
                        },
                        [this, generation](const core::Error& error) {
                            RecordEventError(generation, error);
                        });
                    if (!connected) {
                        RecordEventError(generation, connected.error());
                    }
                }
                if (options.connectConnections) {
                    core::Result<void> connected = events_->ConnectConnections(
                        [state, dispatcher, generation](
                            const mihomo::ConnectionsSnapshot& connections) {
                            if (!dispatcher) {
                                return;
                            }
                            const int count = static_cast<int>(std::min<std::size_t>(
                                connections.connections.size(), 1000000));
                            dispatcher([state, generation, count] {
                                KernelSessionObserver* observer = nullptr;
                                {
                                    std::lock_guard<std::mutex> lock(state->mutex);
                                    if (state->generation != generation ||
                                        state->phase != KernelPhase::Running) {
                                        return;
                                    }
                                    observer = state->observer;
                                }
                                if (observer != nullptr) {
                                    observer->OnKernelConnections(count);
                                }
                            });
                        },
                        1000, [this, generation](const core::Error& error) {
                            RecordEventError(generation, error);
                        });
                    if (!connected) {
                        RecordEventError(generation, connected.error());
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                if (Cancelled() || state_->generation != generation || status->exited.load() ||
                    !process_->IsRunning()) {
                    break;
                }
                state_->phase = KernelPhase::Running;
                state_->version = version.value().version;
                state_->error = {};
                state_->runningProfileId = options.profile.id;
                if (readyResult) {
                    state_->switchResult = *readyResult;
                    state_->pendingSwitch.reset();
                }
            }
            core::MarkStartup("mihomo-api-ready");
            Publish();
            return {};
        }

        lastError = version.error();
        if (lastError.kind == core::ErrorKind::AuthFailed) {
            return core::MakeError(core::ErrorKind::AuthFailed,
                config.imported ? "Mihomo API authentication failed" : lastError.message);
        }
        if (Cancelled()) {
            break;
        }
        if (attempt + 1 < attempts) {
            InterruptibleSleep(options.readinessIntervalMs);
        }
    }

    if (Cancelled() || !IsCurrent(generation)) {
        return cancelled;
    }
    if (status->exited.load() || !process_->IsRunning()) {
        return core::MakeError(core::ErrorKind::StartFailed,
            "mihomo exited during startup (code " + std::to_string(status->exitCode.load()) + ")");
    }
    return core::MakeError(core::ErrorKind::Timeout,
        !config.imported && lastError ? lastError.message : "Mihomo API did not become ready");
}

void KernelSession::HandleProfileSwitch(Command command) {
    auto& result = command.profileSwitch;
    const auto cancel = [&] {
        result.outcome = ProfileSwitchOutcome::Cancelled;
        result.error = core::MakeError(core::ErrorKind::Cancelled, "profile switch was cancelled");
        FinishProfileSwitch(result);
    };
    if (Cancelled()) {
        cancel();
        return;
    }
    if (!IsCurrent(command.generation) || !activeConfig_ || Phase() != KernelPhase::Running) {
        result.outcome = ProfileSwitchOutcome::Rejected;
        result.error = core::MakeError(core::ErrorKind::StartFailed, "the previous kernel is no longer running");
        FinishProfileSwitch(std::move(result));
        return;
    }

    auto candidate = [&]() -> core::Result<std::unique_ptr<LaunchConfig>> {
        try {
            return PrepareLaunch(std::move(command.options), true);
        } catch (...) {
            return core::MakeError(core::ErrorKind::Internal, "could not prepare the profile runtime copy");
        }
    }();
    if (Cancelled()) {
        cancel();
        return;
    }
    if (!candidate) {
        result.outcome = ProfileSwitchOutcome::Rejected;
        result.error = candidate.error();
        FinishProfileSwitch(std::move(result));
        return;
    }

    std::uint64_t generation = 0;
    const auto beginAttempt = [&] {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (Cancelled()) {
            return false;
        }
        generation = ++state_->generation;
        state_->phase = KernelPhase::Starting;
        state_->runningProfileId.clear();
        state_->version.clear();
        state_->eventError.clear();
        state_->error = {};
        return true;
    };
    if (!beginAttempt()) {
        cancel();
        return;
    }
    // Invalidate the old API/event generation before touching its drivers.
    Publish();
    StopEvents();
    StopProcess();

    const auto tryLaunch = [&](const LaunchConfig& config) -> core::Result<void> {
        try {
            return Launch(generation, config, &result);
        } catch (...) {
            return core::MakeError(core::ErrorKind::Internal, "unexpected error starting Mihomo");
        }
    };
    result.outcome = ProfileSwitchOutcome::Succeeded;
    const auto launched = tryLaunch(*candidate.value());
    if (launched) {
        activeConfig_ = std::move(candidate.value());
        return;
    }
    StopEvents();
    StopProcess();
    if (!beginAttempt()) {
        cancel();
        return;
    }

    result.outcome = ProfileSwitchOutcome::RolledBack;
    result.error = launched.error();
    Publish();
    const auto restored = tryLaunch(*activeConfig_);
    if (restored) {
        return;
    }
    if (Cancelled()) {
        cancel();
        return;
    }
    result.outcome = ProfileSwitchOutcome::RollbackFailed;
    result.error = core::MakeError(restored.error().kind,
        "the new profile failed and the previous kernel could not be restored");
    Fail(generation, result.error.kind, result.error.message);
    FinishProfileSwitch(std::move(result));
}

void KernelSession::FinishProfileSwitch(ProfileSwitchResult result) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (shutdownRequested_.load() || !state_->pendingSwitch ||
            state_->pendingSwitch->requestId != result.requestId) {
            return;
        }
        if (cancelRequested_.load() && result.outcome != ProfileSwitchOutcome::Cancelled) {
            result.outcome = ProfileSwitchOutcome::Cancelled;
            result.error = core::MakeError(core::ErrorKind::Cancelled, "profile switch was cancelled");
        }
        state_->switchResult = std::move(result);
        state_->pendingSwitch.reset();
    }
    Publish();
}

void KernelSession::HandleStop(std::uint64_t generation) {
    if (!IsCurrent(generation)) {
        return;
    }
    StopEvents();
    StopProcess();
    activeConfig_.reset();

    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation) {
            state_->phase = KernelPhase::Stopped;
            state_->runningProfileId.clear();
            state_->version.clear();
            state_->error = {};
            state_->eventError.clear();
            publish = true;
        }
    }
    if (publish) {
        Publish();
    }
}

void KernelSession::HandleRestart(std::uint64_t generation) {
    if (!IsCurrent(generation)) {
        return;
    }
    StopEvents();
    StopProcess();
    activeConfig_.reset();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation) {
            state_->phase = KernelPhase::Stopped;
            state_->runningProfileId.clear();
            state_->version.clear();
            state_->error = {};
            state_->eventError.clear();
        }
    }
    Publish();

    if (shutdownRequested_.load()) {
        return;
    }
    std::uint64_t next = 0;
    BeginStartSession(next);
    HandleStart(next);
}

void KernelSession::BeginStartSession(std::uint64_t& generation) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        generation = ++state_->generation;
        state_->phase = KernelPhase::Starting;
        state_->version.clear();
        state_->error = {};
        state_->eventError.clear();
        state_->configPath.clear();
        state_->generatedConfig = false;
        cancelRequested_.store(false);
    }
    Publish();
}

void KernelSession::HandleProcessExited(std::uint64_t generation) {
    if (!IsCurrent(generation) || !processStatus_ || !processStatus_->exited.load()) {
        return;
    }
    KernelPhase phase = KernelPhase::Stopped;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        phase = state_->phase;
    }
    if (phase == KernelPhase::Running || phase == KernelPhase::WaitingForApi ||
        phase == KernelPhase::Starting) {
        Fail(generation, core::ErrorKind::StartFailed,
             "mihomo exited unexpectedly (code " + std::to_string(processStatus_->exitCode.load()) + ")");
    }
}

void KernelSession::Fail(std::uint64_t generation, core::ErrorKind kind, std::string message) {
    if (!IsCurrent(generation)) {
        return;
    }
    StopEvents();
    StopProcess();
    activeConfig_.reset();

    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation && state_->phase != KernelPhase::Stopping &&
            state_->phase != KernelPhase::Stopped) {
            state_->phase = KernelPhase::Failed;
            state_->runningProfileId.clear();
            state_->version.clear();
            state_->error = core::MakeError(kind, std::move(message));
            publish = true;
        }
    }
    if (publish) {
        Publish();
    }
}

void KernelSession::Publish() {
    if (!dispatcher_) {
        return;
    }
    KernelSnapshot snapshot;
    KernelSessionObserver* observer = nullptr;
    std::uint64_t publication = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        publication = ++state_->nextPublication;
        snapshot.phase = state_->phase;
        snapshot.generation = state_->generation;
        snapshot.version = state_->version;
        snapshot.apiAddress = state_->apiAddress;
        snapshot.configPath = state_->configPath;
        snapshot.generatedConfig = state_->generatedConfig;
        snapshot.mixedPort = state_->mixedPort;
        snapshot.error = state_->error;
        snapshot.eventError = state_->eventError;
        snapshot.runningProfileId = state_->runningProfileId;
        snapshot.profileSwitchInProgress = state_->pendingSwitch.has_value();
        snapshot.profileSwitchResult = state_->switchResult;
        observer = state_->observer;
    }
    if (observer == nullptr) {
        return;
    }
    auto state = state_;
    try {
        dispatcher_([state, snapshot, publication] {
            KernelSessionObserver* current = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->generation != snapshot.generation || state->observer == nullptr ||
                    publication <= state->deliveredPublication) {
                    return;
                }
                state->deliveredPublication = publication;
                current = state->observer;
            }
            current->OnKernelSnapshot(snapshot);
        });
    } catch (...) {
        std::fprintf(stderr, "[clashyuka] UI dispatch failed: unknown exception\n");
    }
}

void KernelSession::RecordEventError(std::uint64_t generation, const core::Error& error) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation != generation || !state_->eventError.empty()) {
            return;
        }
        state_->eventError = state_->imported ? "Mihomo event stream is unavailable" : error.message;
    }
    Publish();
}

bool KernelSession::IsCurrent(std::uint64_t generation) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->generation == generation;
}

bool KernelSession::Cancelled() const {
    return cancelRequested_.load() || shutdownRequested_.load();
}

void KernelSession::InterruptibleSleep(int milliseconds) const {
    const int step = 10;
    for (int elapsed = 0; elapsed < milliseconds; elapsed += step) {
        if (Cancelled()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(step, milliseconds - elapsed)));
    }
}

void KernelSession::StopEvents() {
    if (events_ != nullptr) {
        events_->Disconnect();
        events_.reset();
    }
}

void KernelSession::StopProcess() {
    if (processStatus_) {
        processStatus_->active.store(false);
        processStatus_.reset();
    }
    api_.reset();
    if (process_ != nullptr) {
        process_->Stop();
        process_.reset();
    }
}

std::string KernelSession::RecentOutput() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (recentOutput_.empty()) {
        return {};
    }
    std::string text;
    const std::size_t start =
        recentOutput_.size() > 3 ? recentOutput_.size() - 3 : 0;
    for (std::size_t i = start; i < recentOutput_.size(); ++i) {
        if (!text.empty()) {
            text += " | ";
        }
        text += recentOutput_[i];
    }
    if (text.size() > 240) {
        text.resize(240);
        text += "...";
    }
    return "Output: " + text;
}

namespace {

class RealProcessDriver final : public KernelProcessDriver {
public:
    bool Start(const mihomo::MihomoLaunchOptions& options,
               mihomo::MihomoProcessHandlers handlers) override {
        return process_.Start(options, std::move(handlers));
    }
    void Stop() override { process_.Stop(); }
    bool IsRunning() const override { return process_.IsRunning(); }
    std::optional<int> ExitCode() const override { return process_.GetExitCode(); }

private:
    mihomo::MihomoProcess process_;
};

class RealApiDriver final : public KernelApiDriver {
public:
    explicit RealApiDriver(mihomo::MihomoApiConfig config) : client_(std::move(config)) {}

    core::Result<mihomo::MihomoVersion> GetVersion() override { return client_.GetVersion(); }

private:
    mihomo::MihomoApiClient client_;
};

class RealEventDriver final : public KernelEventDriver {
public:
    explicit RealEventDriver(mihomo::MihomoEventConfig config) : client_(std::move(config)) {}

    core::Result<void> ConnectTraffic(TrafficHandler handler, ErrorHandler onError) override {
        return client_.ConnectTraffic(std::move(handler), std::move(onError));
    }

    core::Result<void> ConnectConnections(ConnectionHandler handler, int intervalMs,
                                          ErrorHandler onError) override {
        return client_.ConnectConnections(std::move(handler), intervalMs, std::move(onError));
    }

    void Disconnect() override { client_.Disconnect(); }

private:
    mihomo::MihomoEventClient client_;
};

class RealDriverFactory final : public KernelDriverFactory {
public:
    std::unique_ptr<KernelProcessDriver> CreateProcess() override {
        return std::make_unique<RealProcessDriver>();
    }
    std::unique_ptr<KernelApiDriver> CreateApi(const mihomo::MihomoApiConfig& config) override {
        return std::make_unique<RealApiDriver>(config);
    }
    std::unique_ptr<KernelEventDriver> CreateEvents(
        const mihomo::MihomoEventConfig& config) override {
        return std::make_unique<RealEventDriver>(config);
    }
};

}  // namespace

std::unique_ptr<KernelDriverFactory> CreateDefaultDriverFactory() {
    return std::make_unique<RealDriverFactory>();
}

}  // namespace clashyuka::runtime
