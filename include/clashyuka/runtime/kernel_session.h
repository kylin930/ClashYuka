#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "clashyuka/core/result.h"
#include "clashyuka/mihomo/api_client.h"
#include "clashyuka/mihomo/event_client.h"
#include "clashyuka/mihomo/process.h"
#include "clashyuka/mihomo/types.h"

namespace clashyuka::runtime {

enum class KernelPhase {
    Stopped,
    Starting,
    WaitingForApi,
    Running,
    Stopping,
    Failed,
};

const char* KernelPhaseName(KernelPhase phase);

// Resolve imported bytes on the ProfileStore owner thread before submitting.
// A disengaged yaml means the built-in/legacy runtime configuration.
struct KernelProfile {
    std::string id = "builtin";
    std::optional<std::string> yaml;
};

enum class ProfileSwitchOutcome { Succeeded, Rejected, RolledBack, RollbackFailed, Cancelled };

struct ProfileSwitchResult {
    std::uint64_t requestId = 0;
    std::string oldProfileId;
    std::string newProfileId;
    ProfileSwitchOutcome outcome = ProfileSwitchOutcome::Cancelled;
    core::Error error;
    // Sanitized `mihomo -t` output explaining a failed check. Empty on success.
    std::vector<std::string> diagnostics;
};

enum class StreamAvailability { Waiting, Available, Disconnected, Disabled };

struct StreamStatus {
    StreamAvailability availability = StreamAvailability::Waiting;
    std::string error;
};

struct ConnectionCloseResult {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    std::string connectionId;
    core::Error error;
};

enum class RulesLoadStatus { Idle, Ready, Failed };
enum class RuleProviderUpdateStatus { Idle, Updating, Updated, UpdateFailed, ReadbackFailed };

struct RulesSnapshot {
    std::shared_ptr<const mihomo::RuleCollection> rules;
    std::shared_ptr<const mihomo::RuleProviderCollection> providers;
    RulesLoadStatus rulesStatus = RulesLoadStatus::Idle;
    RulesLoadStatus providersStatus = RulesLoadStatus::Idle;
    RuleProviderUpdateStatus updateStatus = RuleProviderUpdateStatus::Idle;
    bool loading = false;
    bool operator==(const RulesSnapshot&) const = default;
};

struct KernelOptions {
    std::string executablePath;
    std::string workingDirectory;
    std::string configPath;
    std::string controllerHost = "127.0.0.1";
    int controllerPort = 9090;
    int mixedPort = 7897;
    std::string secret;

    // Readiness budget. The worst case is approximately
    // readinessAttempts * (readinessTimeoutMs + readinessIntervalMs).
    int readinessAttempts = 30;
    int readinessIntervalMs = 100;
    int readinessTimeoutMs = 400;

    bool connectTraffic = true;
    bool connectConnections = true;
    KernelProfile profile;
    // Opt-in `mihomo -t`. The default matches the reference client: start the
    // kernel directly so cached data is reused and no network probe blocks a switch.
    bool preflight = false;
    int preflightTimeoutMs = 30000;
    bool connectLogs = true;
    // Known subscription URLs (controller secret is included automatically).
    std::vector<std::string> logRedactions;
    std::string geoSourcePreset = "profile";
};

struct KernelSnapshot {
    KernelPhase phase = KernelPhase::Stopped;
    std::uint64_t generation = 0;
    std::string version;
    std::string apiAddress;
    std::string configPath;
    bool generatedConfig = false;
    int mixedPort = 0;
    core::Error error;
    // Legacy aggregate for recoverable stream errors. Prefer the per-stream statuses.
    std::string eventError;
    std::string runningProfileId;
    bool profileSwitchInProgress = false;
    // Last terminal switch result. Deduplicate by requestId; Running alone is not success.
    std::optional<ProfileSwitchResult> profileSwitchResult;
    StreamStatus trafficStream;
    StreamStatus connectionStream;
    StreamStatus logStream;
    RulesSnapshot rules;
    bool rulesAvailable = false;
};

class KernelSessionObserver {
public:
    virtual ~KernelSessionObserver() = default;
    virtual void OnKernelSnapshot(const KernelSnapshot& snapshot) = 0;
    virtual void OnKernelTraffic(double, double) {}
    virtual void OnKernelConnections(int) {}
    // Full replacement snapshot. A valid empty snapshot is delivered too.
    virtual void OnKernelConnections(std::uint64_t, const mihomo::ConnectionsSnapshot& snapshot) {
        OnKernelConnections(static_cast<int>(std::min<std::size_t>(snapshot.connections.size(), 1000000)));
    }
    virtual void OnKernelLogs(std::uint64_t, const std::vector<mihomo::MihomoLogEntry>&) {}
    virtual void OnKernelConnectionClose(const ConnectionCloseResult&) {}
};

// Driver interfaces keep KernelSession testable without a real kernel, network
// or process. Production drivers wrap the P1 MihomoProcess/ApiClient/EventClient.
class KernelProcessDriver {
public:
    virtual ~KernelProcessDriver() = default;
    virtual bool Start(const mihomo::MihomoLaunchOptions& options,
                       mihomo::MihomoProcessHandlers handlers) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual std::optional<int> ExitCode() const = 0;
};

class KernelApiDriver {
public:
    virtual ~KernelApiDriver() = default;
    virtual core::Result<mihomo::MihomoVersion> GetVersion() = 0;
    virtual core::Result<void> CloseConnection(std::string_view id) = 0;
    virtual core::Result<mihomo::RuleCollection> GetRules() = 0;
    virtual core::Result<mihomo::RuleProviderCollection> GetRuleProviders() = 0;
    virtual core::Result<void> UpdateRuleProvider(std::string_view name) = 0;
};

class KernelEventDriver {
public:
    using TrafficHandler = std::function<void(const mihomo::TrafficStatistics&)>;
    using ConnectionHandler = std::function<void(const mihomo::ConnectionsSnapshot&)>;
    using ErrorHandler = std::function<void(const core::Error&)>;
    using LogHandler = std::function<void(const mihomo::MihomoLogEntry&)>;

    virtual ~KernelEventDriver() = default;
    virtual core::Result<void> ConnectTraffic(TrafficHandler handler, ErrorHandler onError) = 0;
    virtual core::Result<void> ConnectConnections(ConnectionHandler handler, int intervalMs,
                                                  ErrorHandler onError) = 0;
    virtual core::Result<void> ConnectLogs(LogHandler handler, ErrorHandler onError) = 0;
    virtual void Disconnect() = 0;
};

class KernelDriverFactory {
public:
    virtual ~KernelDriverFactory() = default;
    virtual std::unique_ptr<KernelProcessDriver> CreateProcess() = 0;
    virtual std::unique_ptr<KernelApiDriver> CreateApi(const mihomo::MihomoApiConfig& config) = 0;
    virtual std::unique_ptr<KernelEventDriver> CreateEvents(
        const mihomo::MihomoEventConfig& config) = 0;
};

std::unique_ptr<KernelDriverFactory> CreateDefaultDriverFactory();

// Serializes every lifecycle operation on one worker thread. Start/Stop/Restart
// are non-blocking; Shutdown blocks (bounded) until the worker has stopped the
// process and joined. UI updates are delivered through the dispatcher, which
// the application wires to the wxWidgets main thread.
class KernelSession final {
public:
    using Dispatcher = std::function<void(std::function<void()>)>;

    KernelSession(std::unique_ptr<KernelDriverFactory> drivers, Dispatcher dispatcher);
    ~KernelSession();

    KernelSession(const KernelSession&) = delete;
    KernelSession& operator=(const KernelSession&) = delete;

    void SetObserver(KernelSessionObserver* observer);

    // New options apply to the next Start/Restart, except logRedactions,
    // which also applies to incoming frames in the active stream. Callable from any thread.
    void SetOptions(KernelOptions options);
    KernelOptions Options() const;

    void Start();
    void Stop();
    void Restart();
    // Running-only. A rejected submission returns nullopt and has no terminal result.
    // Stop cancels an accepted switch; Restart is ignored while one is pending.
    std::optional<std::uint64_t> SwitchProfile(KernelProfile profile);
    // Only one close may be pending. Rejected submissions have no result callback.
    // Leaving Running invalidates the request and suppresses its result.
    std::optional<std::uint64_t> CloseConnection(std::uint64_t generation, std::string id);
    // A provider name submits PUT followed by fresh reads of both collections.
    bool RefreshRules(std::uint64_t generation, std::string providerName = {});
    void Shutdown();

    KernelPhase Phase() const;
    KernelSnapshot Snapshot() const;
    // Effective controller configuration (including the secret) for in-process
    // services such as the proxy catalog. Never expose this to UI text.
    mihomo::MihomoApiConfig ApiConfig() const;

private:
    struct SharedState;
    struct LaunchConfig;
    struct ProcessStatus;
    enum class CommandType { Start, Stop, Restart, SwitchProfile, CloseConnection, RefreshRules, ProcessExited };
    struct Command {
        CommandType type = CommandType::Start;
        std::uint64_t generation = 0;
        KernelOptions options;
        ProfileSwitchResult profileSwitch;
        ConnectionCloseResult connectionClose;
        std::string ruleProviderName;
    };

    void Enqueue(Command command);
    void WorkerMain();
    void HandleStart(std::uint64_t generation);
    void HandleStop(std::uint64_t generation);
    void HandleRestart(std::uint64_t generation);
    void HandleProcessExited(std::uint64_t generation);
    void HandleProfileSwitch(Command command);
    void FinishProfileSwitch(ProfileSwitchResult result);
    core::Result<std::unique_ptr<LaunchConfig>> PrepareLaunch(
        KernelOptions options, bool switching, std::vector<std::string>* diagnostics = nullptr);
    core::Result<void> Preflight(const LaunchConfig& config,
                                 std::vector<std::string>* diagnostics = nullptr);
    void AppendRuntimeLog(std::uint64_t generation, std::string type, std::string_view line);
    core::Result<void> Launch(std::uint64_t generation, const LaunchConfig& config,
                              ProfileSwitchResult* readyResult = nullptr);
    void HandleShutdown();
    void BeginStartSession(std::uint64_t& generation);
    void Fail(std::uint64_t generation, core::ErrorKind kind, std::string message);
    void Publish();
    void StartEvents(std::uint64_t generation, const KernelOptions& options,
                     const mihomo::MihomoApiConfig& apiConfig);
    static void DispatchStreams(const std::shared_ptr<SharedState>& state,
                                const Dispatcher& dispatcher, std::uint64_t generation);
    void HandleCloseConnection(ConnectionCloseResult result);
    void HandleRefreshRules(std::uint64_t generation, const std::string& providerName);
    bool IsCurrent(std::uint64_t generation) const;
    bool Cancelled() const;
    void InterruptibleSleep(int milliseconds) const;
    void StopEvents();
    void StopProcess();
    std::string RecentOutput() const;

    std::unique_ptr<KernelDriverFactory> drivers_;
    Dispatcher dispatcher_;
    std::shared_ptr<SharedState> state_;

    mutable std::mutex optionsMutex_;
    KernelOptions options_;

    mutable std::mutex outputMutex_;
    std::deque<std::string> recentOutput_;

    std::thread worker_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::condition_variable exitCv_;
    std::deque<Command> queue_;
    std::atomic<bool> shutdownRequested_{false};
    bool workerExited_ = false;

    std::atomic<bool> cancelRequested_{false};

    // Worker-thread-only objects.
    std::unique_ptr<LaunchConfig> activeConfig_;
    std::shared_ptr<ProcessStatus> processStatus_;
    std::unique_ptr<KernelProcessDriver> process_;
    std::unique_ptr<KernelApiDriver> api_;
    std::unique_ptr<KernelEventDriver> events_;
};

}  // namespace clashyuka::runtime
