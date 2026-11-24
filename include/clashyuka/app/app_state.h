#pragma once

#include <memory>
#include <string>
#include <vector>

#include <wx/string.h>

#include "clashyuka/runtime/kernel_session.h"
#include "clashyuka/runtime/profile_store.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::runtime {
struct ProxyCatalog;
struct KernelUpdateSnapshot;
struct AppUpdateSnapshot;
}

namespace clashyuka::app {

enum class PageId {
    Overview,
    Proxies,
    Connections,
    Rules,
    Profiles,
    Logs,
    Settings,
};

enum class KernelState { NotStarted, Starting, WaitingForApi, Running, Stopping, Error };

enum class ApiState { Disconnected, Connecting, Connected };

enum class ProxyMode { Rule, Global, Direct };

struct ProfileRow {
    std::string id;
    std::string name;
    runtime::ProfileKind kind;
    runtime::ProfileRefreshOutcome refreshOutcome;
    long long refreshCompletedAt;
    bool refreshing;

    bool operator==(const ProfileRow&) const = default;
};

struct ProfilesSnapshot {
    std::vector<ProfileRow> rows;
    std::string selectedProfileId;
    std::string runningProfileId;
    std::string storeError;
    std::string actionStatus;
    bool subscriptionsAvailable = false;
    bool addingSubscription = false;
    bool switching = false;
    bool runningCopyOutdated = false;

    wxString NameFor(const std::string& id, const wxString& fallback) const;
    bool operator==(const ProfilesSnapshot&) const = default;
};

struct PendingConnectionClose {
    std::uint64_t requestId;
    std::uint64_t generation;
    std::string connectionId;
    bool awaitingSnapshot = false;

    bool operator==(const PendingConnectionClose&) const = default;
};

struct LiveStreamsSnapshot {
    std::uint64_t generation = 0;
    bool running = false;
    bool switching = false;
    runtime::StreamAvailability logsStatus = runtime::StreamAvailability::Waiting;
    runtime::StreamAvailability connectionsStatus = runtime::StreamAvailability::Waiting;
    bool connectionsFresh = false;
    std::shared_ptr<const std::vector<mihomo::MihomoLogEntry>> logs;
    std::shared_ptr<const mihomo::ConnectionsSnapshot> connections;
    std::optional<PendingConnectionClose> pendingClose;

    bool CanClose(std::uint64_t expectedGeneration, std::string_view id) const;
};

// Immutable-looking snapshot consumed by the shell, pages and (later) the
// Mihomo integration. The controller is the only component that mutates it.
struct AppStateSnapshot {
    KernelState kernel = KernelState::NotStarted;
    ApiState api = ApiState::Disconnected;
    PageId currentPage = PageId::Overview;

    ui::ColorTheme theme = ui::ColorTheme::Purple;
    bool dark = false;
    bool animationsEnabled = true;
    bool reducedMotion = false;

    ProxyMode proxyMode = ProxyMode::Rule;

    std::shared_ptr<const ProfilesSnapshot> profiles;
    std::string mihomoVersion;

    double uploadBytesPerSecond = 0.0;
    double downloadBytesPerSecond = 0.0;
    bool trafficAvailable = false;
    int connectionCount = 0;
    LiveStreamsSnapshot live;
    runtime::RulesSnapshot rules;
    bool rulesAvailable = false;

    std::string kernelPath;
    std::string apiAddress;
    // Friendly, user-facing text for the last kernel failure. Technical detail
    // stays in the runtime log.
    std::string kernelError;

    // Shared, immutable proxy catalog. A shared_ptr keeps AppState copies cheap
    // even for large proxy collections.
    std::shared_ptr<const runtime::ProxyCatalog> proxyCatalog;
    std::shared_ptr<const runtime::KernelUpdateSnapshot> kernelUpdate;
    std::shared_ptr<const runtime::AppUpdateSnapshot> appUpdate;

    bool systemProxyEnabled = false;
    bool systemProxyActive = false;
    std::string systemProxyAddress;
    std::string systemProxyStatus;
    bool systemProxyRetryAvailable = false;
};

class AppStateObserver {
public:
    virtual ~AppStateObserver() = default;
    virtual void OnAppStateChanged(const AppStateSnapshot& state) = 0;
};

// Single source of truth for application-level state. Observers are notified
// synchronously on the GUI thread; the controller guarantees that.
class AppState {
public:
    const AppStateSnapshot& Get() const { return snapshot_; }

    void AddObserver(AppStateObserver* observer);
    void RemoveObserver(AppStateObserver* observer);

    void SetKernelState(KernelState state);
    void SetApiState(ApiState state);
    void SetCurrentPage(PageId page);
    void SetTheme(ui::ColorTheme theme);
    void SetDark(bool dark);
    void SetAnimationsEnabled(bool enabled);
    void SetReducedMotion(bool reduced);
    void SetProxyMode(ProxyMode mode);
    void SetProfiles(ProfilesSnapshot profiles);
    void SetMihomoVersion(std::string version);
    void SetTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond, bool available);
    void SetConnectionCount(int count);
    void UpdateLiveStreams(const runtime::KernelSnapshot& kernel);
    void ResetLiveStreams();
    void PublishConnections(std::uint64_t generation, const mihomo::ConnectionsSnapshot& snapshot);
    void AppendLogs(std::uint64_t generation, const std::vector<mihomo::MihomoLogEntry>& batch);
    void ClearLogs();
    bool RememberRedaction(std::string value);
    bool BeginConnectionClose(PendingConnectionClose request);
    bool FinishConnectionClose(const runtime::ConnectionCloseResult& result);
    void SetRules(bool available, runtime::RulesSnapshot rules = {});
    void SetKernelPath(std::string path);
    void SetApiAddress(std::string address);
    void SetKernelError(std::string error);
    void SetProxyCatalog(std::shared_ptr<const runtime::ProxyCatalog> catalog);
    void SetKernelUpdate(std::shared_ptr<const runtime::KernelUpdateSnapshot> update);
    void SetAppUpdate(std::shared_ptr<const runtime::AppUpdateSnapshot> update);
    void SetSystemProxy(bool enabled, bool active, std::string address,
                        std::string status = {}, bool retryAvailable = false);

private:
    friend class AppController;
    const std::vector<std::string>& Redactions() const { return redactions_; }
    std::string Redact(std::string text) const;
    void ScrubConnections(const mihomo::ConnectionsSnapshot& snapshot, bool fresh);
    void Notify();

    AppStateSnapshot snapshot_;
    std::vector<std::string> redactions_;
    std::vector<AppStateObserver*> observers_;
};

}  // namespace clashyuka::app
