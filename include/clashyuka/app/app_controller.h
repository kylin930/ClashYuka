#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <wx/string.h>
#include <wx/timer.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/platform/system_proxy.h"
#include "clashyuka/runtime/kernel_installer.h"
#include "clashyuka/runtime/kernel_session.h"
#include "clashyuka/runtime/proxy_service.h"
#include "clashyuka/runtime/settings_store.h"
#include "clashyuka/runtime/subscription_service.h"

namespace clashyuka::app {

// UI services the controller needs from the application shell.
class AppShell {
public:
    virtual ~AppShell() = default;
    virtual void ShowMessage(const wxString& message) = 0;
    virtual void ShowInfoDialog(const wxString& title, const wxString& body) = 0;
    // Stage a replacement without releasing the current registration.
    virtual bool PrepareWindowHotkey(const runtime::WindowHotkey&) { return false; }
    virtual void FinishWindowHotkey(bool /*commit*/) {}
    // Returns true when the user accepts the action.
    virtual bool ShowConfirmDialog(const wxString&, const wxString&, const wxString&) {
        return false;
    }
};

// Coordinates navigation, theme preferences, application state and the Mihomo
// runtime session. Pages must not mutate global state or touch Mihomo directly;
// they call this controller.
class AppController final : public runtime::KernelSessionObserver,
                            public runtime::ProxyServiceObserver,
                            private wxEvtHandler {
public:
    explicit AppController(AppShell* shell);
    ~AppController() override;

    AppState& State() { return state_; }
    const AppStateSnapshot& Snapshot() const { return state_.Get(); }

    void Navigate(PageId page);

    void SetDarkMode(bool dark);
    void ToggleDarkMode();
    void SetColorTheme(ui::ColorTheme theme);
    void SetAnimationsEnabled(bool enabled);
    void SetReducedMotion(bool reduced);
    void SetProxyMode(ProxyMode mode);

    void ShowMessage(const wxString& message);
    // Explains why a feature is unavailable instead of pretending success.
    void ShowNotImplemented(const wxString& feature, const wxString& detail = {});

    // P2.2 runtime integration. InitializeRuntime must run on the GUI thread.
    void InitializeRuntime();
    void ShutdownRuntime();
    void StartKernel();
    void StopKernel();
    void RestartKernel();

    const runtime::AppSettings& Settings() const { return settings_; }
    bool RuntimeInitialized() const { return runtimeInitialized_; }
    bool SaveKernelSettings(const runtime::AppSettings& settings, wxString* error,
                            bool* restartRequired);
    bool AutoStartKernelEnabled() const { return settings_.autoStartKernel; }
    bool SaveDesktopSettings(bool closeToTray, const wxString& shortcut, wxString& error);
    bool WindowHotkeyActive() const { return windowHotkeyActive_; }
    bool SystemProxyPreferenceAvailable() const { return runtimeInitialized_ && systemProxy_; }

    // KernelSessionObserver, always invoked on the GUI thread.
    void OnKernelSnapshot(const runtime::KernelSnapshot& snapshot) override;
    void OnKernelTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond) override;
    void OnKernelConnections(std::uint64_t generation,
                             const mihomo::ConnectionsSnapshot& snapshot) override;
    void OnKernelLogs(std::uint64_t generation,
                      const std::vector<mihomo::MihomoLogEntry>& batch) override;
    void OnKernelConnectionClose(const runtime::ConnectionCloseResult& result) override;
    void ClearLogs();
    void CloseConnection(std::uint64_t generation, std::string id);
    void OnProxyCatalog(const std::shared_ptr<const runtime::ProxyCatalog>& catalog) override;

    void RefreshRules();
    void RefreshRuleProvider(std::string name);
    void RefreshProxies();
    void SelectProxy(std::string group, std::string target);
    void ClearProxySelection(std::string group);
    void TestProxyDelay(std::string name);
    void TestGroupDelays(std::vector<std::string> names);
    void CancelProxyTests();
    void SetSystemProxyEnabled(bool enabled);
    void RetrySystemProxy();

    void ImportProfile(const wxString& path);
    void RenameProfile(const std::string& id, const wxString& name);
    void DeleteProfile(const std::string& id);
    void ActivateProfile(const std::string& id);
    std::optional<runtime::SubscriptionSettings> SubscriptionForEdit(const std::string& id);
    void AddSubscription(const wxString& name, const runtime::SubscriptionSettings& settings);
    void EditSubscription(const std::string& id, const runtime::SubscriptionSettings& settings);
    void RefreshSubscription(const std::string& id);
    void CancelSubscription(const std::string& id);

    void CheckAppUpdate();
    void CancelAppUpdate();
    void OpenAppReleasePage();

    // Kernel release installation. All network/file work runs on a background
    // worker; snapshots are published on the GUI thread.
    void CheckKernelUpdate();
    void DownloadKernelUpdate(bool allowUnverifiedChecksum = false);
    void CancelKernelUpdate();
    void UseInstalledKernel();
    bool KernelUpdateBusy() const { return installerBusy_.load(); }

private:
    void SyncThemeState();
    runtime::KernelOptions BuildKernelOptions() const;
    void RememberSubscriptionUrls();
    void RememberSubscriptionUrl(const std::string& url);
    core::Result<runtime::KernelProfile> LoadProfile(const std::string& id) const;
    bool PrepareSelectedKernel();
    void PublishProfiles();
    void TickSubscriptions();
    void ProfileMessage(const wxString& message);
    void PersistProfileSelection(const std::string& id);
    void ConsumeProfileSwitch(const runtime::KernelSnapshot& snapshot);
    bool CanDeleteProfile(const std::string& id) const;
    void CreateProxyService();
    void DestroyProxyService();
    void SyncSystemProxy();
    void CreateInstaller();
    void StartInstallerThread(std::function<void()> task);
    void StopInstallerThread();
    void PublishKernelUpdate(const std::function<void(runtime::KernelUpdateSnapshot&)>& mutate);
    void PostToUi(std::function<void()> task);

    AppState state_;
    AppShell* shell_ = nullptr;

    std::unique_ptr<runtime::ProfileStore> profileStore_;
    std::unique_ptr<runtime::SubscriptionService> subscriptions_;
    std::unique_ptr<wxTimer> subscriptionTimer_;
    std::optional<runtime::SubscriptionService::JobId> addingSubscription_;
    std::optional<std::uint64_t> pendingProfileSwitch_;
    std::uint64_t switchRevision_ = 0;
    std::uint64_t launchRevision_ = 0;
    std::uint64_t runningRevision_ = 0;
    // Counts successful publishes during this process. Record the revision at
    // launch submission so a concurrent refresh never marks its newer bytes ready.
    std::unordered_map<std::string, std::uint64_t> profileRevisions_;
    std::string runningProfileId_;
    std::string launchProfileId_;
    std::string profileStoreError_;
    std::string profileActionStatus_;

    std::unique_ptr<runtime::KernelSession> kernel_;
    bool confirmingConnectionClose_ = false;
    std::unique_ptr<runtime::ProxyService> proxyService_;
    std::uint64_t proxyServiceGeneration_ = 0;
    std::shared_ptr<int> uiLifetime_;
    std::unique_ptr<platform::SystemProxyController> systemProxy_;
    int systemProxyPort_ = 0;
    runtime::AppSettings settings_;
    std::string settingsPath_;
    std::string settingsDirectory_;
    bool runtimeInitialized_ = false;
    bool windowHotkeyActive_ = false;
    bool systemProxyRecoveryOnly_ = false;
    std::uint64_t lastShownFailure_ = 0;
    std::uint64_t lastShownTrafficWarning_ = 0;
    std::uint64_t lastShownProxyError_ = 0;

    void PublishAppUpdate(runtime::AppUpdateSnapshot update);
    void StopAppUpdate();
    std::thread appUpdateThread_;
    std::shared_ptr<std::atomic<bool>> appUpdateCancel_;

    std::unique_ptr<runtime::KernelInstaller> installer_;
    std::thread installerThread_;
    std::atomic<bool> installerCancel_{false};
    std::atomic<bool> installerBusy_{false};
    std::mutex installerMutex_;
    std::string installRoot_;
    runtime::KernelRelease latestRelease_;
    runtime::KernelAsset latestAsset_;
    bool latestReleaseReady_ = false;
    std::shared_ptr<runtime::KernelUpdateSnapshot> updateSnapshot_;
};

}  // namespace clashyuka::app
