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
    core::Error error;
    std::string eventError;
    KernelSessionObserver* observer = nullptr;
    mihomo::MihomoApiConfig apiConfig;
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
    snapshot.error = state_->error;
    snapshot.eventError = state_->eventError;
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
        if (phase == KernelPhase::Starting || phase == KernelPhase::WaitingForApi ||
            phase == KernelPhase::Running || phase == KernelPhase::Stopping) {
            return;
        }
        generation = ++state_->generation;
        state_->phase = KernelPhase::Starting;
        state_->version.clear();
        state_->error = {};
        state_->eventError.clear();
        state_->configPath.clear();
        state_->generatedConfig = false;
    }
    cancelRequested_.store(false);
    processExited_.store(false);
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
    }
    cancelRequested_.store(true);
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
    }
    cancelRequested_.store(active);
    processExited_.store(false);
    Publish();
    Enqueue({active ? CommandType::Restart : CommandType::Start, generation});
}

void KernelSession::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        shutdownRequested_.store(true);
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

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->observer = nullptr;
    }
}

void KernelSession::Enqueue(Command command) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (shutdownRequested_.load()) {
            return;
        }
        queue_.push_back(command);
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
                command = queue_.front();
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
                case CommandType::ProcessExited:
                    HandleProcessExited(command.generation);
                    break;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[clashyuka] kernel worker error: %s\n", error.what());
            Fail(command.generation, core::ErrorKind::Internal,
                 "unexpected error in the kernel worker");
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
    }
    Publish();
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

    const KernelOptions options = Options();
    std::error_code fileError;
    if (options.executablePath.empty() ||
        !fs::is_regular_file(PathFromUtf8(options.executablePath), fileError) || fileError) {
        Fail(generation, core::ErrorKind::KernelNotFound,
             "mihomo executable was not found at '" + options.executablePath + "'");
        return;
    }
    if (options.workingDirectory.empty() || options.configPath.empty()) {
        Fail(generation, core::ErrorKind::ConfigFailed,
             "the runtime directory or config path is not configured");
        return;
    }

    RuntimeConfigDefinition definition;
    definition.host = options.controllerHost;
    definition.port = options.controllerPort;
    definition.mixedPort = options.mixedPort;
    definition.secret = options.secret;

    core::Result<RuntimeConfigInfo> config = EnsureRuntimeConfig(options.configPath, definition);
    if (!config) {
        Fail(generation, core::ErrorKind::ConfigFailed, config.error().message);
        return;
    }
    const RuntimeConfigInfo info = config.value();

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation != generation) {
            return;
        }
        state_->apiAddress = FormatAddress(info.host, info.port);
        state_->configPath = options.configPath;
        state_->generatedConfig = info.generated;
    }
    Publish();

    if (Cancelled() || !IsCurrent(generation)) {
        return;
    }

    processExited_.store(false);
    processExitCode_.store(0);
    process_ = drivers_->CreateProcess();
    if (!process_) {
        Fail(generation, core::ErrorKind::StartFailed, "process driver is unavailable");
        return;
    }

    mihomo::MihomoApiConfig apiConfig;
    apiConfig.host = info.host;
    apiConfig.port = info.port;
    apiConfig.secret = info.secret;
    apiConfig.connectionTimeoutMs = options.readinessTimeoutMs;
    apiConfig.readTimeoutMs = options.readinessTimeoutMs;
    apiConfig.kernelRunningProbe = [this] { return process_ && process_->IsRunning(); };
    api_ = drivers_->CreateApi(apiConfig);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->apiConfig = apiConfig;
    }

    mihomo::MihomoLaunchOptions launch;
    launch.executablePath = options.executablePath;
    launch.arguments = {"-d", options.workingDirectory, "-f", options.configPath};

    mihomo::MihomoProcessHandlers handlers;
    handlers.onStdoutLine = [this](std::string_view line) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        if (line.empty()) {
            return;
        }
        recentOutput_.emplace_back(line.substr(0, 300));
        if (recentOutput_.size() > kRecentOutputLimit) {
            recentOutput_.pop_front();
        }
    };
    handlers.onStderrLine = handlers.onStdoutLine;
    handlers.onExited = [this, generation](int exitCode) {
        processExitCode_.store(exitCode);
        processExited_.store(true);
        if (IsCurrent(generation)) {
            cancelRequested_.store(true);
            Enqueue({CommandType::ProcessExited, generation});
        }
        queueCv_.notify_all();
    };

    if (!process_->Start(launch, std::move(handlers))) {
        Fail(generation, core::ErrorKind::StartFailed,
             "could not start mihomo. " + RecentOutput());
        return;
    }
    core::MarkStartup("mihomo-process-start");

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation != generation) {
            return;
        }
        state_->phase = KernelPhase::WaitingForApi;
    }
    Publish();

    const int attempts = std::max(1, options.readinessAttempts);
    core::Error lastError;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (Cancelled()) {
            break;
        }
        core::Result<mihomo::MihomoVersion> version = api_ ? api_->GetVersion()
                                                           : core::Result<mihomo::MihomoVersion>(
                                                                 core::MakeError(
                                                                     core::ErrorKind::StartFailed,
                                                                     "API driver is unavailable"));
        if (version) {
            if (Cancelled() || !IsCurrent(generation)) {
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
                if (state_->generation != generation) {
                    return;
                }
                state_->phase = KernelPhase::Running;
                state_->version = version.value().version;
                state_->error = {};
            }
            core::MarkStartup("mihomo-api-ready");
            Publish();
            return;
        }

        lastError = version.error();
        if (lastError.kind == core::ErrorKind::AuthFailed) {
            Fail(generation, core::ErrorKind::AuthFailed, lastError.message);
            return;
        }
        if (Cancelled()) {
            break;
        }
        if (attempt + 1 < attempts) {
            InterruptibleSleep(options.readinessIntervalMs);
        }
    }

    if (processExited_.load()) {
        Fail(generation, core::ErrorKind::StartFailed,
             "mihomo exited during startup (code " + std::to_string(processExitCode_.load()) + ")");
        return;
    }
    if (Cancelled() || !IsCurrent(generation)) {
        // Stop/Shutdown owns the final state. Release local resources only.
        StopEvents();
        StopProcess();
        return;
    }
    Fail(generation, core::ErrorKind::Timeout,
         lastError ? lastError.message : "mihomo API did not become ready");
}

void KernelSession::HandleStop(std::uint64_t generation) {
    StopEvents();
    StopProcess();

    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation) {
            state_->phase = KernelPhase::Stopped;
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
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation) {
            state_->phase = KernelPhase::Stopped;
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
    }
    cancelRequested_.store(false);
    processExited_.store(false);
    Publish();
}

void KernelSession::HandleProcessExited(std::uint64_t generation) {
    if (!IsCurrent(generation)) {
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
             "mihomo exited unexpectedly (code " + std::to_string(processExitCode_.load()) + ")");
    }
}

void KernelSession::Fail(std::uint64_t generation, core::ErrorKind kind, std::string message) {
    if (!IsCurrent(generation)) {
        return;
    }
    StopEvents();
    StopProcess();

    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation == generation && state_->phase != KernelPhase::Stopping &&
            state_->phase != KernelPhase::Stopped) {
            state_->phase = KernelPhase::Failed;
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
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        snapshot.phase = state_->phase;
        snapshot.generation = state_->generation;
        snapshot.version = state_->version;
        snapshot.apiAddress = state_->apiAddress;
        snapshot.configPath = state_->configPath;
        snapshot.generatedConfig = state_->generatedConfig;
        snapshot.error = state_->error;
        snapshot.eventError = state_->eventError;
        observer = state_->observer;
    }
    if (observer == nullptr) {
        return;
    }
    auto state = state_;
    try {
        dispatcher_([state, snapshot] {
            KernelSessionObserver* current = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->generation != snapshot.generation ||
                    state->observer == nullptr) {
                    return;
                }
                current = state->observer;
            }
            current->OnKernelSnapshot(snapshot);
        });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[clashyuka] UI dispatch failed: %s\n", error.what());
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
        state_->eventError = error.message;
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
