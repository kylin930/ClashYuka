#include "clashyuka/app/app_controller.h"

#include <utility>

#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::app {

AppController::AppController(AppShell* shell) : shell_(shell) {
    SyncThemeState();
}

void AppController::SyncThemeState() {
    ui::ThemeManager& theme = ui::ThemeManager::Instance();
    state_.SetTheme(theme.GetColorTheme());
    state_.SetDark(theme.IsDark());
    state_.SetAnimationsEnabled(theme.AnimationsEnabled());
    state_.SetReducedMotion(theme.ReducedMotion());
}

void AppController::Navigate(PageId page) {
    state_.SetCurrentPage(page);
}

void AppController::SetDarkMode(bool dark) {
    ui::ThemeManager::Instance().SetDarkMode(dark);
    state_.SetDark(dark);
}

void AppController::ToggleDarkMode() {
    SetDarkMode(!ui::ThemeManager::Instance().IsDark());
}

void AppController::SetColorTheme(ui::ColorTheme theme) {
    ui::ThemeManager::Instance().SetColorTheme(theme);
    state_.SetTheme(theme);
}

void AppController::SetAnimationsEnabled(bool enabled) {
    ui::ThemeManager::Instance().SetAnimationsEnabled(enabled);
    state_.SetAnimationsEnabled(enabled);
}

void AppController::SetReducedMotion(bool reduced) {
    ui::ThemeManager::Instance().SetReducedMotion(reduced);
    state_.SetReducedMotion(reduced);
}

void AppController::SetProxyMode(ProxyMode mode) {
    state_.SetProxyMode(mode);
}

void AppController::ShowMessage(const wxString& message) {
    if (shell_ != nullptr) {
        shell_->ShowMessage(message);
    }
}

void AppController::ShowNotImplemented(const wxString& feature, const wxString& detail) {
    if (shell_ == nullptr) {
        return;
    }
    wxString body = feature + " is not available in this build.";
    if (!detail.empty()) {
        body += "\n\n";
        body += detail;
    }
    body += "\n\nNo action was performed.";
    shell_->ShowInfoDialog(feature, body);
}

void AppController::ReportKernelState(KernelState kernel) {
    state_.SetKernelState(kernel);
}

void AppController::ReportApiState(ApiState api) {
    state_.SetApiState(api);
}

void AppController::ReportMihomoVersion(std::string version) {
    state_.SetMihomoVersion(std::move(version));
}

void AppController::ReportProfile(std::string name, bool loaded) {
    state_.SetProfile(std::move(name), loaded);
}

void AppController::ReportTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond,
                                  bool available) {
    state_.SetTraffic(uploadBytesPerSecond, downloadBytesPerSecond, available);
}

void AppController::ReportConnectionCount(int count) {
    state_.SetConnectionCount(count);
}

}  // namespace clashyuka::app
