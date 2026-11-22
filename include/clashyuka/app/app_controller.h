#pragma once

#include <string>

#include <wx/string.h>

#include "clashyuka/app/app_state.h"

namespace clashyuka::app {

// UI services the controller needs from the application shell.
class AppShell {
public:
    virtual ~AppShell() = default;
    virtual void ShowMessage(const wxString& message) = 0;
    virtual void ShowInfoDialog(const wxString& title, const wxString& body) = 0;
};

// Coordinates navigation, theme preferences and application state. Pages must
// not mutate global state or touch Mihomo directly; they call this controller.
class AppController {
public:
    explicit AppController(AppShell* shell);

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

    // P2.2 integration hooks. They only publish state for now.
    void ReportKernelState(KernelState state);
    void ReportApiState(ApiState state);
    void ReportMihomoVersion(std::string version);
    void ReportProfile(std::string name, bool loaded);
    void ReportTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond,
                       bool available);
    void ReportConnectionCount(int count);

private:
    void SyncThemeState();

    AppState state_;
    AppShell* shell_ = nullptr;
};

}  // namespace clashyuka::app
