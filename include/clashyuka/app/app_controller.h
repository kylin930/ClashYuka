#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <wx/string.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/runtime/kernel_session.h"
#include "clashyuka/runtime/settings_store.h"

namespace clashyuka::app {

// UI services the controller needs from the application shell.
class AppShell {
public:
    virtual ~AppShell() = default;
    virtual void ShowMessage(const wxString& message) = 0;
    virtual void ShowInfoDialog(const wxString& title, const wxString& body) = 0;
};

// Coordinates navigation, theme preferences, application state and the Mihomo
// runtime session. Pages must not mutate global state or touch Mihomo directly;
// they call this controller.
class AppController final : public runtime::KernelSessionObserver {
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

private:
    void SyncThemeState();
    runtime::KernelOptions BuildKernelOptions() const;

    AppState state_;
    AppShell* shell_ = nullptr;

    std::unique_ptr<runtime::KernelSession> kernel_;
    runtime::AppSettings settings_;
    std::string settingsPath_;
    std::string settingsDirectory_;
    bool runtimeInitialized_ = false;
    std::uint64_t lastShownFailure_ = 0;
    std::uint64_t lastShownEventWarning_ = 0;
};

}  // namespace clashyuka::app
