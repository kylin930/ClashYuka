#pragma once

#include <string>
#include <vector>

#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::app {

enum class PageId {
    Overview,
    Proxies,
    Connections,
    Profiles,
    Logs,
    Settings,
};

enum class KernelState { NotStarted, Starting, Running, Stopping, Error };

enum class ApiState { Disconnected, Connecting, Connected };

enum class ProxyMode { Rule, Global, Direct };

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

    std::string profileName;
    bool profileLoaded = false;
    std::string mihomoVersion;

    double uploadBytesPerSecond = 0.0;
    double downloadBytesPerSecond = 0.0;
    bool trafficAvailable = false;
    int connectionCount = 0;

    std::string kernelPath;
    std::string apiAddress;
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
    void SetProfile(std::string name, bool loaded);
    void SetMihomoVersion(std::string version);
    void SetTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond, bool available);
    void SetConnectionCount(int count);
    void SetKernelPath(std::string path);
    void SetApiAddress(std::string address);

private:
    void Notify();

    AppStateSnapshot snapshot_;
    std::vector<AppStateObserver*> observers_;
};

}  // namespace clashyuka::app
