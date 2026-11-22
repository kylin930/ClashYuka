#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <wx/string.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/runtime/kernel_installer.h"
#include "clashyuka/runtime/kernel_session.h"
#include "clashyuka/runtime/proxy_service.h"
#include "clashyuka/runtime/settings_store.h"

namespace clashyuka::app {

// UI services the controller needs from the application shell.
class AppShell {
public:
    virtual ~AppShell() = default;
    virtual void ShowMessage(const wxString& message) = 0;
    virtual void ShowInfoDialog(const wxString& title, const wxString& body) = 0;
    // Returns true when the user accepts the action.
    virtual bool ShowConfirmDialog(const wxString&, const wxString&, const wxString&) {
        return false;
    }
};

// Coordinates navigation, theme preferences, application state and the Mihomo
// runtime session. Pages must not mutate global state or touch Mihomo directly;
// they call this controller.
class AppController final : public runtime::KernelSessionObserver,
                            public runtime::ProxyServiceObserver {
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

    // KernelSessionObserver, always invoked on the GUI thread.
    void OnKernelSnapshot(const runtime::KernelSnapshot& snapshot) override;
    void OnKernelTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond) override;
    void OnKernelConnections(int count) override;
    void OnProxyCatalog(const std::shared_ptr<const runtime::ProxyCatalog>& catalog) override;

    void RefreshProxies();
    void SelectProxy(std::string group, std::string target);
    void ClearProxySelection(std::string group);
    void TestProxyDelay(std::string name);
    void TestGroupDelays(std::vector<std::string> names);
    void CancelProxyTests();

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
    void CreateProxyService();
    void DestroyProxyService();
    void CreateInstaller();
    void StartInstallerThread(std::function<void()> task);
    void StopInstallerThread();
    void PublishKernelUpdate(const std::function<void(runtime::KernelUpdateSnapshot&)>& mutate);
    void PostToUi(std::function<void()> task);

    AppState state_;
    AppShell* shell_ = nullptr;

    std::unique_ptr<runtime::KernelSession> kernel_;
    std::unique_ptr<runtime::ProxyService> proxyService_;
    runtime::AppSettings settings_;
    std::string settingsPath_;
    std::string settingsDirectory_;
    bool runtimeInitialized_ = false;
    std::uint64_t lastShownFailure_ = 0;
    std::uint64_t lastShownEventWarning_ = 0;
    std::uint64_t lastShownProxyError_ = 0;

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
