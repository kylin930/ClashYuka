#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

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
    int preflightTimeoutMs = 5000;
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
    // Recoverable event-stream failure. The kernel keeps running.
    std::string eventError;
    std::string runningProfileId;
    bool profileSwitchInProgress = false;
    // Last terminal switch result. Deduplicate by requestId; Running alone is not success.
    std::optional<ProfileSwitchResult> profileSwitchResult;
};

class KernelSessionObserver {
public:
    virtual ~KernelSessionObserver() = default;
    virtual void OnKernelSnapshot(const KernelSnapshot& snapshot) = 0;
    virtual void OnKernelTraffic(double, double) {}
    virtual void OnKernelConnections(int) {}
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
};

class KernelEventDriver {
public:
    using TrafficHandler = std::function<void(const mihomo::TrafficStatistics&)>;
    using ConnectionHandler = std::function<void(const mihomo::ConnectionsSnapshot&)>;
    using ErrorHandler = std::function<void(const core::Error&)>;

    virtual ~KernelEventDriver() = default;
    virtual core::Result<void> ConnectTraffic(TrafficHandler handler, ErrorHandler onError) = 0;
    virtual core::Result<void> ConnectConnections(ConnectionHandler handler, int intervalMs,
                                                  ErrorHandler onError) = 0;
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

    // New options apply to the next Start/Restart. Callable from any thread.
    void SetOptions(KernelOptions options);
    KernelOptions Options() const;

    void Start();
    void Stop();
    void Restart();
    // Running-only. A rejected submission returns nullopt and has no terminal result.
    // Stop cancels an accepted switch; Restart is ignored while one is pending.
    std::optional<std::uint64_t> SwitchProfile(KernelProfile profile);
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
    enum class CommandType { Start, Stop, Restart, SwitchProfile, ProcessExited };
    struct Command {
        CommandType type = CommandType::Start;
        std::uint64_t generation = 0;
        KernelOptions options;
        ProfileSwitchResult profileSwitch;
    };

    void Enqueue(Command command);
    void WorkerMain();
    void HandleStart(std::uint64_t generation);
    void HandleStop(std::uint64_t generation);
    void HandleRestart(std::uint64_t generation);
    void HandleProcessExited(std::uint64_t generation);
    void HandleProfileSwitch(Command command);
    void FinishProfileSwitch(ProfileSwitchResult result);
    core::Result<std::unique_ptr<LaunchConfig>> PrepareLaunch(KernelOptions options, bool switching);
    core::Result<void> Preflight(const LaunchConfig& config);
    core::Result<void> Launch(std::uint64_t generation, const LaunchConfig& config,
                              ProfileSwitchResult* readyResult = nullptr);
    void HandleShutdown();
    void BeginStartSession(std::uint64_t& generation);
    void Fail(std::uint64_t generation, core::ErrorKind kind, std::string message);
    void Publish();
    void RecordEventError(std::uint64_t generation, const core::Error& error);
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
