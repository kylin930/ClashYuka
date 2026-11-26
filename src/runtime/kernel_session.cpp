#include "clashyuka/runtime/kernel_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>

#include "clashyuka/core/timing.h"
#include "clashyuka/runtime/runtime_config.h"

namespace clashyuka::runtime {
namespace {

namespace fs = std::filesystem;

constexpr int kShutdownWaitMs = 15000;
constexpr std::size_t kRecentOutputLimit = 8;
constexpr std::size_t kPreflightDiagnosticLimit = 20;
constexpr std::size_t kPendingLogLimit = 500;
constexpr std::size_t kLogPayloadBytes = 4096;

std::string SanitizeLogField(std::string_view text, const std::vector<std::string>& redactions,
                             std::string_view activeSecret, std::size_t maxBytes) {
    // Match against the original field, including overlapping values, before truncating.
    std::string clean;
    std::size_t copied = 0;
    for (std::size_t pos = 0; pos < text.size() && clean.size() < maxBytes;) {
        std::size_t start = std::string_view::npos;
        std::size_t end = 0;
        const auto consider = [&](std::string_view value) {
            if (value.empty()) {
                return;
            }
            const auto match = text.find(value, pos);
            if (match != std::string_view::npos && match <= start) {
                end = match == start ? std::max(end, match + value.size()) : match + value.size();
                start = match;
            }
        };
        consider(activeSecret);
        for (const auto& value : redactions) {
            consider(value);
        }
        if (start == std::string_view::npos) {
            break;
        }
        if (start >= copied) {
            clean.append(text.substr(copied, std::min(start - copied, maxBytes + 1 - clean.size())));
            clean += "[redacted]";
        }
        copied = std::max(copied, end);
        pos = start + 1;
    }
    if (clean.size() <= maxBytes) {
        clean.append(text.substr(copied, maxBytes + 1 - clean.size()));
    }
    if (clean.size() > maxBytes) {
        while (maxBytes > 0 && (static_cast<unsigned char>(clean[maxBytes]) & 0xC0) == 0x80) {
            --maxBytes;
        }
        clean.resize(maxBytes);
    }
    return clean;
}

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
    StreamStatus trafficStream;
    StreamStatus connectionStream;
    StreamStatus logStream;
    std::shared_ptr<const mihomo::ConnectionsSnapshot> connections;
    std::optional<mihomo::TrafficStatistics> traffic;
    std::deque<mihomo::MihomoLogEntry> logs;
    std::vector<std::string> logRedactions;
    bool connectionsDirty = false;
    bool streamStatusDirty = false;
    bool streamDispatchPending = false;
    bool acceptingEvents = false;
    std::uint64_t nextCloseId = 0;
    std::uint64_t pendingCloseId = 0;
    RulesSnapshot rules;
    KernelSessionObserver* observer = nullptr;
    mihomo::MihomoApiConfig apiConfig;
    std::string runningProfileId;
    std::optional<ProfileSwitchResult> pendingSwitch;
    std::optional<ProfileSwitchResult> switchResult;
    std::uint64_t nextSwitchId = 0;
    std::uint64_t nextPublication = 0;
    std::uint64_t deliveredPublication = 0;

    bool IsStreaming(std::uint64_t expectedGeneration) const {
        return generation == expectedGeneration && phase == KernelPhase::Running && acceptingEvents;
    }

    bool CanAccessRules(std::uint64_t expectedGeneration) const {
        return IsStreaming(expectedGeneration) && !pendingSwitch;
    }

    bool HasConnection(const std::string& id) const {
        return !id.empty() && connections &&
               std::any_of(connections->connections.begin(), connections->connections.end(),
                           [&id](const auto& connection) { return connection.id == id; });
    }

    void ResetStreams() {
        acceptingEvents = false;
        trafficStream = {};
        connectionStream = {};
        logStream = {};
        connections.reset();
        traffic.reset();
        logs.clear();
        connectionsDirty = false;
        streamStatusDirty = false;
        streamDispatchPending = false;
        pendingCloseId = 0;
        rules = {};
    }

    KernelSnapshot MakeSnapshot() const {
        KernelSnapshot snapshot;
        snapshot.phase = phase;
        snapshot.generation = generation;
        snapshot.version = version;
        snapshot.apiAddress = apiAddress;
        snapshot.configPath = configPath;
        snapshot.generatedConfig = generatedConfig;
        snapshot.mixedPort = mixedPort;
        snapshot.error = error;
        snapshot.runningProfileId = runningProfileId;
        snapshot.profileSwitchInProgress = pendingSwitch.has_value();
        snapshot.profileSwitchResult = switchResult;
        snapshot.trafficStream = trafficStream;
        snapshot.connectionStream = connectionStream;
        snapshot.logStream = logStream;
        snapshot.rulesAvailable = CanAccessRules(generation);
        if (snapshot.rulesAvailable) {
            snapshot.rules = rules;
        }
        for (const auto* stream : {&trafficStream, &connectionStream, &logStream}) {
            if (!stream->error.empty()) {
                snapshot.eventError = stream->error;
                break;
            }
        }
        return snapshot;
    }
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
        DispatchStreams(state_, dispatcher_, Snapshot().generation);
    }
}

void KernelSession::SetOptions(KernelOptions options) {
    std::scoped_lock lock(optionsMutex_, state_->mutex);
    if (state_->logRedactions != options.logRedactions) {
        // Pending frames were already truncated with the old rules.
        state_->logs.clear();
        state_->logRedactions = options.logRedactions;
    }
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
    return state_->MakeSnapshot();
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
        state_->ResetStreams();
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
            state_->ResetStreams();
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
        state_->rules = {};
        queue_.push_back(std::move(command));
    }
    Publish();
    queueCv_.notify_all();
    return requestId;
}

std::optional<std::uint64_t> KernelSession::CloseConnection(std::uint64_t generation, std::string id) {
    std::uint64_t requestId;
    {
        std::scoped_lock lock(state_->mutex, queueMutex_);
        if (shutdownRequested_.load() || !state_->IsStreaming(generation) || state_->pendingSwitch ||
            state_->pendingCloseId != 0 || !state_->HasConnection(id)) {
            return std::nullopt;
        }
        requestId = ++state_->nextCloseId;
        Command command;
        command.type = CommandType::CloseConnection;
        command.generation = generation;
        command.connectionClose = {requestId, generation, std::move(id), {}};
        queue_.push_back(std::move(command));
        state_->pendingCloseId = requestId;
    }
    queueCv_.notify_all();
    return requestId;
}

bool KernelSession::RefreshRules(std::uint64_t generation, std::string providerName) {
    {
        std::scoped_lock lock(state_->mutex, queueMutex_);
        auto& rules = state_->rules;
        if (shutdownRequested_.load() || !state_->CanAccessRules(generation) || rules.loading) {
            return false;
        }
        if (!providerName.empty()) {
            if (!rules.providers) {
                return false;
            }
            const auto& providers = rules.providers->providers;
            const auto provider = std::ranges::find(providers, providerName, &mihomo::RuleProviderInfo::name);
            if (provider == providers.end() || !provider->CanRefresh()) {
                return false;
            }
        }
        Command command;
        command.type = CommandType::RefreshRules;
        command.generation = generation;
        const bool updating = !providerName.empty();
        command.ruleProviderName = std::move(providerName);
        queue_.push_back(std::move(command));
        rules.loading = true;
        rules.updateStatus = updating ? RuleProviderUpdateStatus::Updating
                                      : RuleProviderUpdateStatus::Idle;
    }
    Publish();
    queueCv_.notify_all();
    return true;
}

void KernelSession::Shutdown() {
    {
        std::scoped_lock lock(state_->mutex, queueMutex_);
        shutdownRequested_.store(true);
        state_->observer = nullptr;
        state_->ResetStreams();
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
                case CommandType::CloseConnection:
                    HandleCloseConnection(std::move(command.connectionClose));
                    break;
                case CommandType::RefreshRules:
                    HandleRefreshRules(command.generation, command.ruleProviderName);
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
        state_->ResetStreams();
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
    KernelOptions options, bool switching, std::vector<std::string>* diagnostics) {
    (void)switching;
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
    auto copy = imported ? MakeProfileRuntimeCopy(options.workingDirectory, *options.profile.yaml,
                                                   definition, options.geoSourcePreset)
                         : MakeLegacyRuntimeCopy(options.workingDirectory, options.configPath,
                                                  definition, options.geoSourcePreset);
    if (!copy) {
        return copy.error();
    }
    options.profile.yaml.reset();
    auto config = std::make_unique<LaunchConfig>(
        LaunchConfig{std::move(options), std::move(copy.value()), imported});
    if (options.preflight && (imported || switching)) {
        const auto checked = Preflight(*config, diagnostics);
        if (!checked) {
            return checked.error();
        }
    }
    return config;
}

void KernelSession::AppendRuntimeLog(std::uint64_t generation, std::string type,
                                     std::string_view line) {
    if (line.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->generation != generation) {
            return;
        }
        if (state_->logs.size() == kPendingLogLimit) {
            state_->logs.pop_front();
        }
        state_->logs.push_back(
            {SanitizeLogField(type, state_->logRedactions, state_->apiConfig.secret, 32),
             SanitizeLogField(line, state_->logRedactions, state_->apiConfig.secret,
                              kLogPayloadBytes)});
    }
    DispatchStreams(state_, dispatcher_, generation);
}

core::Result<void> KernelSession::Preflight(const LaunchConfig& config,
                                            std::vector<std::string>* diagnostics) {
    using namespace std::chrono;
    const int limitMs = std::clamp(config.options.preflightTimeoutMs, 1, 60000);
    const auto deadline = steady_clock::now() + milliseconds(limitMs);
    std::mutex captureMutex;
    std::vector<std::string> captured;
    const auto capture = [&](std::string_view line) {
        if (line.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(captureMutex);
        if (captured.size() == kPreflightDiagnosticLimit) {
            captured.erase(captured.begin());
        }
        captured.emplace_back(line.substr(0, 300));
    };
    // Runs after the process is stopped so the reader threads no longer touch captured.
    const auto collect = [&](std::string summary) {
        std::lock_guard<std::mutex> lock(captureMutex);
        if (diagnostics == nullptr) {
            return;
        }
        diagnostics->push_back(std::move(summary));
        for (const auto& line : captured) {
            diagnostics->push_back(SanitizeLogField(line, config.options.logRedactions,
                                                    config.options.secret, 300));
        }
        captured.clear();
    };
    auto process = drivers_->CreateProcess();
    if (Cancelled()) {
        return core::MakeError(core::ErrorKind::Cancelled, "profile preflight was cancelled");
    }
    mihomo::MihomoProcessHandlers handlers;
    handlers.onStdoutLine = capture;
    handlers.onStderrLine = capture;
    if (!process || !process->Start({config.options.executablePath,
                                    {"-t", "-d", config.copy->workingDirectory,
                                     "-f", config.copy->configPath}}, handlers)) {
        return core::MakeError(core::ErrorKind::StartFailed, "could not start Mihomo profile preflight");
    }
    while (!Cancelled() && steady_clock::now() < deadline) {
        if (const auto code = process->ExitCode()) {
            process->Stop();
            if (*code == 0) {
                return {};
            }
            collect("Mihomo exited with code " + std::to_string(*code) + " during the profile check.");
            return core::MakeError(core::ErrorKind::ProfilePreflightRejected,
                                   "Mihomo rejected the profile configuration");
        }
        InterruptibleSleep(10);
    }
    process->Stop();
    if (Cancelled()) {
        collect("The profile check was cancelled.");
        return core::MakeError(core::ErrorKind::Cancelled, "profile preflight was cancelled");
    }
    collect("The profile check did not finish within " + std::to_string(limitMs) + " ms.");
    return core::MakeError(core::ErrorKind::Timeout, "Mihomo profile preflight timed out");
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
    handlers.onStdoutLine = [this, generation, imported = config.imported](std::string_view line) {
        if (line.empty()) {
            return;
        }
        if (!imported) {
            // Legacy diagnostics stay available for start error messages.
            std::lock_guard<std::mutex> lock(outputMutex_);
            recentOutput_.emplace_back(line.substr(0, 300));
            if (recentOutput_.size() > kRecentOutputLimit) {
                recentOutput_.pop_front();
            }
        }
        const std::string_view text(line.substr(0, 400));
        const std::string type = text.find("level=error") != std::string_view::npos   ? "error"
                                 : text.find("level=warn") != std::string_view::npos  ? "warning"
                                                                                     : "info";
        AppendRuntimeLog(generation, type, line);
    };
    handlers.onStderrLine = handlers.onStdoutLine;
    handlers.onExited = [this, generation, status](int exitCode) {
        status->exitCode.store(exitCode);
        status->exited.store(true);
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (!status->active.load() || state_->generation != generation) {
                return;
            }
            state_->acceptingEvents = false;
        }
        Enqueue({CommandType::ProcessExited, generation});
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

            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                if (Cancelled() || state_->generation != generation || status->exited.load() ||
                    !process_->IsRunning()) {
                    break;
                }
                state_->phase = KernelPhase::Running;
                state_->acceptingEvents = true;
                // Buffer first frames until the Running snapshot is queued.
                state_->streamDispatchPending = true;
                const auto initialStatus = [](bool enabled) {
                    return StreamStatus{enabled ? StreamAvailability::Waiting : StreamAvailability::Disabled, {}};
                };
                state_->trafficStream = initialStatus(options.connectTraffic);
                state_->connectionStream = initialStatus(options.connectConnections);
                state_->logStream = initialStatus(options.connectLogs);
                state_->version = version.value().version;
                state_->error = {};
                state_->runningProfileId = options.profile.id;
                if (readyResult) {
                    state_->switchResult = *readyResult;
                    state_->pendingSwitch.reset();
                }
            }
            StartEvents(generation, options, apiConfig);
            core::MarkStartup("mihomo-api-ready");
            Publish();
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->streamDispatchPending = false;
            }
            DispatchStreams(state_, dispatcher_, generation);
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

void KernelSession::HandleCloseConnection(ConnectionCloseResult result) {
    auto state = state_;
    const auto current = [&] {
        return state->IsStreaming(result.generation) && state->pendingCloseId == result.requestId;
    };
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!current()) {
            return;
        }
        if (!state->HasConnection(result.connectionId)) {
            result.error = core::MakeError(core::ErrorKind::Cancelled, "Connection is no longer available");
        }
    }
    if (!result.error) {
        try {
            const auto closed = api_->CloseConnection(result.connectionId);
            if (!closed) {
                result.error = core::MakeError(closed.error().kind, "Mihomo could not close the connection",
                                               closed.error().httpStatus);
            }
        } catch (...) {
            result.error = core::MakeError(core::ErrorKind::Internal, "Mihomo could not close the connection");
        }
    }
    const auto resetPending = [&] {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (current()) {
            state->pendingCloseId = 0;
        }
    };
    if (!dispatcher_) {
        resetPending();
        return;
    }
    try {
        dispatcher_([state, result] {
            KernelSessionObserver* observer = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->IsStreaming(result.generation) || state->pendingCloseId != result.requestId) {
                    return;
                }
                state->pendingCloseId = 0;
                observer = state->observer;
            }
            if (observer) {
                observer->OnKernelConnectionClose(result);
            }
        });
    } catch (...) {
        resetPending();
    }
}

void KernelSession::HandleRefreshRules(std::uint64_t generation, const std::string& providerName) {
    const auto current = [&] {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->CanAccessRules(generation);
    };
    if (!current()) {
        return;
    }

    RulesSnapshot result;
    if (!providerName.empty()) {
        result.updateStatus = RuleProviderUpdateStatus::UpdateFailed;
        try {
            if (api_->UpdateRuleProvider(providerName)) {
                result.updateStatus = RuleProviderUpdateStatus::ReadbackFailed;
            }
        } catch (...) {
            // The view reports the failed operation without exposing controller details.
        }
    }
    if (result.updateStatus != RuleProviderUpdateStatus::UpdateFailed) {
        if (!current()) {
            return;
        }
        result.rulesStatus = RulesLoadStatus::Failed;
        try {
            auto rules = api_->GetRules();
            if (rules) {
                result.rules = std::make_shared<const mihomo::RuleCollection>(std::move(rules.value()));
                result.rulesStatus = RulesLoadStatus::Ready;
            }
        } catch (...) {
            // An error in one collection must not prevent reading the other.
        }
        if (!current()) {
            return;
        }
        result.providersStatus = RulesLoadStatus::Failed;
        try {
            auto providers = api_->GetRuleProviders();
            if (providers) {
                result.providers = std::make_shared<const mihomo::RuleProviderCollection>(
                    std::move(providers.value()));
                result.providersStatus = RulesLoadStatus::Ready;
            }
        } catch (...) {
        }
        if (!providerName.empty() && result.rules && result.providers) {
            const auto& providers = result.providers->providers;
            if (std::ranges::find(providers, providerName, &mihomo::RuleProviderInfo::name) != providers.end()) {
                result.updateStatus = RuleProviderUpdateStatus::Updated;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->CanAccessRules(generation)) {
            return;
        }
        auto& rules = state_->rules;
        if (result.rules) {
            rules.rules = std::move(result.rules);
        }
        if (result.providers) {
            rules.providers = std::move(result.providers);
        }
        if (result.updateStatus != RuleProviderUpdateStatus::UpdateFailed) {
            rules.rulesStatus = result.rulesStatus;
            rules.providersStatus = result.providersStatus;
        }
        rules.updateStatus = result.updateStatus;
        rules.loading = false;
    }
    Publish();
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
            return PrepareLaunch(std::move(command.options), true, &result.diagnostics);
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
        state_->ResetStreams();
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
            state_->ResetStreams();
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
            state_->ResetStreams();
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
        state_->ResetStreams();
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
            state_->ResetStreams();
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
        snapshot = state_->MakeSnapshot();
        observer = state_->observer;
    }
    if (observer == nullptr) {
        return;
    }
    auto state = state_;
    try {
        dispatcher_([state, snapshot, publication]() mutable {
            KernelSessionObserver* current = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->generation != snapshot.generation || state->observer == nullptr ||
                    publication <= state->deliveredPublication) {
                    return;
                }
                state->deliveredPublication = publication;
                if (snapshot.rulesAvailable) {
                    // A profile switch can clear rules before the generation advances.
                    snapshot.rulesAvailable = state->CanAccessRules(snapshot.generation);
                    snapshot.rules = snapshot.rulesAvailable ? state->rules : RulesSnapshot{};
                }
                current = state->observer;
            }
            current->OnKernelSnapshot(snapshot);
        });
    } catch (...) {
        std::fprintf(stderr, "[clashyuka] UI dispatch failed: unknown exception\n");
    }
}

void KernelSession::StartEvents(std::uint64_t generation, const KernelOptions& options,
                                const mihomo::MihomoApiConfig& apiConfig) {
    if (!options.connectTraffic && !options.connectConnections && !options.connectLogs) {
        return;
    }
    mihomo::MihomoEventConfig config;
    config.api = apiConfig;
    events_ = drivers_->CreateEvents(config);

    auto state = state_;
    auto dispatcher = dispatcher_;
    auto update = [state, dispatcher, generation](StreamStatus SharedState::*stream,
                                                    const char* error, auto store) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->IsStreaming(generation)) {
                return;
            }
            auto& status = state.get()->*stream;
            const auto availability = error ? StreamAvailability::Disconnected
                                            : StreamAvailability::Available;
            if (status.availability != availability) {
                status = {availability, error ? error : ""};
                state->streamStatusDirty = true;
            }
            store(*state);
        }
        DispatchStreams(state, dispatcher, generation);
    };
    auto reportError = [update](StreamStatus SharedState::*stream, const char* message) {
        return [update, stream, message](const core::Error&) {
            update(stream, message, [stream](SharedState& state) {
                if (stream == &SharedState::connectionStream) {
                    state.connections.reset();
                    state.connectionsDirty = false;
                } else if (stream == &SharedState::trafficStream) {
                    state.traffic.reset();
                }
            });
        };
    };
    const auto unavailable = core::MakeError(core::ErrorKind::ConnectionFailed,
                                             "Mihomo event driver is unavailable");
    if (options.connectTraffic) {
        auto onError = reportError(&SharedState::trafficStream, "Mihomo traffic stream is unavailable");
        const auto connected = events_ ? events_->ConnectTraffic(
            [update](const mihomo::TrafficStatistics& traffic) {
                update(&SharedState::trafficStream, nullptr,
                       [&traffic](SharedState& state) { state.traffic = traffic; });
            }, onError) : core::Result<void>(unavailable);
        if (!connected) {
            onError(connected.error());
        }
    }
    if (options.connectConnections) {
        auto onError = reportError(&SharedState::connectionStream, "Mihomo connection stream is unavailable");
        const auto connected = events_ ? events_->ConnectConnections(
            [update](const mihomo::ConnectionsSnapshot& connections) {
                update(&SharedState::connectionStream, nullptr, [&connections](SharedState& state) {
                    state.connections = std::make_shared<mihomo::ConnectionsSnapshot>(connections);
                    state.connectionsDirty = true;
                });
            }, 1000, onError) : core::Result<void>(unavailable);
        if (!connected) {
            onError(connected.error());
        }
    }
    if (options.connectLogs) {
        auto onError = reportError(&SharedState::logStream, "Mihomo log stream is unavailable");
        const auto connected = events_ ? events_->ConnectLogs(
            [update](const mihomo::MihomoLogEntry& entry) {
                update(&SharedState::logStream, nullptr, [&entry](SharedState& state) {
                    if (state.logs.size() == kPendingLogLimit) {
                        state.logs.pop_front();
                    }
                    state.logs.push_back({
                        SanitizeLogField(entry.type, state.logRedactions, state.apiConfig.secret, 32),
                        SanitizeLogField(entry.payload, state.logRedactions, state.apiConfig.secret,
                                         kLogPayloadBytes)});
                });
            }, onError) : core::Result<void>(unavailable);
        if (!connected) {
            onError(connected.error());
        }
    }
}

void KernelSession::DispatchStreams(const std::shared_ptr<SharedState>& state,
                                    const Dispatcher& dispatcher, std::uint64_t generation) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const bool hasUpdates = state->streamStatusDirty || state->connectionsDirty ||
                                state->traffic.has_value() || !state->logs.empty();
        if (!dispatcher || !state->observer || !state->IsStreaming(generation) ||
            state->streamDispatchPending || !hasUpdates) {
            return;
        }
        state->streamDispatchPending = true;
    }
    try {
        dispatcher([state, generation] {
            std::optional<KernelSnapshot> snapshot;
            std::optional<mihomo::TrafficStatistics> traffic;
            std::shared_ptr<const mihomo::ConnectionsSnapshot> connections;
            std::deque<mihomo::MihomoLogEntry> logs;
            KernelSessionObserver* observer = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->IsStreaming(generation)) {
                    return;
                }
                state->streamDispatchPending = false;
                observer = state->observer;
                if (!observer) {
                    return;
                }
                if (std::exchange(state->streamStatusDirty, false)) {
                    snapshot = state->MakeSnapshot();
                    state->deliveredPublication = ++state->nextPublication;
                }
                traffic = std::exchange(state->traffic, std::nullopt);
                if (std::exchange(state->connectionsDirty, false)) {
                    connections = state->connections;
                }
                logs.swap(state->logs);
            }
            const auto stillCurrent = [&] {
                std::lock_guard<std::mutex> lock(state->mutex);
                return state->IsStreaming(generation) && state->observer == observer;
            };
            if (snapshot && stillCurrent()) {
                observer->OnKernelSnapshot(*snapshot);
            }
            if (traffic && stillCurrent()) {
                observer->OnKernelTraffic(traffic->up, traffic->down);
            }
            if (connections && stillCurrent()) {
                observer->OnKernelConnections(generation, *connections);
            }
            if (!logs.empty() && stillCurrent()) {
                const std::vector<mihomo::MihomoLogEntry> batch(std::make_move_iterator(logs.begin()),
                                                             std::make_move_iterator(logs.end()));
                observer->OnKernelLogs(generation, batch);
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->IsStreaming(generation)) {
            state->streamDispatchPending = false;
        }
    }
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
    explicit RealApiDriver(mihomo::MihomoApiConfig config)
        : client_(config), rulesClient_(RulesConfig(std::move(config))) {}

    core::Result<mihomo::MihomoVersion> GetVersion() override { return client_.GetVersion(); }
    core::Result<void> CloseConnection(std::string_view id) override {
        return client_.CloseConnection(id);
    }
    core::Result<mihomo::RuleCollection> GetRules() override { return rulesClient_.GetRules(); }
    core::Result<mihomo::RuleProviderCollection> GetRuleProviders() override {
        return rulesClient_.GetRuleProviders();
    }
    core::Result<void> UpdateRuleProvider(std::string_view name) override {
        return rulesClient_.UpdateRuleProvider(name);
    }

private:
    static mihomo::MihomoApiConfig RulesConfig(mihomo::MihomoApiConfig config) {
        // Rule provider updates may wait for Mihomo to fetch a remote provider.
        config.readTimeoutMs = 5000;
        return config;
    }

    mihomo::MihomoApiClient client_;
    mihomo::MihomoApiClient rulesClient_;
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

    core::Result<void> ConnectLogs(LogHandler handler, ErrorHandler onError) override {
        return client_.ConnectLogs(std::move(handler), "debug", std::move(onError));
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
