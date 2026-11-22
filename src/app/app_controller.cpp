#include "clashyuka/app/app_controller.h"

#include <cstdio>
#include <filesystem>
#include <utility>

#include <wx/app.h>
#include <wx/stdpaths.h>

#include "clashyuka/app/app_text.h"
#include "clashyuka/core/timing.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::app {
namespace {

namespace fs = std::filesystem;

fs::path PathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string PathToUtf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string DefaultSettingsPath() {
    const wxString directory = wxStandardPaths::Get().GetUserDataDir();
    return PathToUtf8(PathFromUtf8(directory.utf8_string()) / "settings.json");
}

std::string FormatAddress(const std::string& host, int port) {
    if (host.find(':') != std::string::npos && !host.starts_with('[')) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

KernelState MapKernelPhase(runtime::KernelPhase phase) {
    switch (phase) {
        case runtime::KernelPhase::Stopped:
            return KernelState::NotStarted;
        case runtime::KernelPhase::Starting:
            return KernelState::Starting;
        case runtime::KernelPhase::WaitingForApi:
            return KernelState::WaitingForApi;
        case runtime::KernelPhase::Running:
            return KernelState::Running;
        case runtime::KernelPhase::Stopping:
            return KernelState::Stopping;
        case runtime::KernelPhase::Failed:
            return KernelState::Error;
    }
    return KernelState::NotStarted;
}

ApiState MapApiPhase(runtime::KernelPhase phase) {
    switch (phase) {
        case runtime::KernelPhase::WaitingForApi:
            return ApiState::Connecting;
        case runtime::KernelPhase::Running:
            return ApiState::Connected;
        case runtime::KernelPhase::Stopped:
        case runtime::KernelPhase::Starting:
        case runtime::KernelPhase::Stopping:
        case runtime::KernelPhase::Failed:
            return ApiState::Disconnected;
    }
    return ApiState::Disconnected;
}

bool IsSessionActive(const runtime::KernelSession* session) {
    if (session == nullptr) {
        return false;
    }
    const runtime::KernelPhase phase = session->Phase();
    return phase == runtime::KernelPhase::Starting ||
           phase == runtime::KernelPhase::WaitingForApi ||
           phase == runtime::KernelPhase::Running;
}

}  // namespace

AppController::AppController(AppShell* shell) : shell_(shell) {
    SyncThemeState();
    settings_ = runtime::DefaultSettings();
}

AppController::~AppController() {
    ShutdownRuntime();
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

void AppController::InitializeRuntime() {
    if (runtimeInitialized_) {
        return;
    }

    settingsPath_ = DefaultSettingsPath();
    const runtime::SettingsLoadResult loaded = runtime::LoadSettings(settingsPath_);
    settings_ = loaded.settings;
    settingsDirectory_ = PathToUtf8(PathFromUtf8(settingsPath_).parent_path());

    state_.SetKernelPath(settings_.mihomoPath);
    state_.SetApiAddress(FormatAddress(settings_.controllerHost, settings_.controllerPort));
    state_.SetKernelError("");

    kernel_ = std::make_unique<runtime::KernelSession>(
        runtime::CreateDefaultDriverFactory(), [](std::function<void()> task) {
            if (wxTheApp != nullptr) {
                wxTheApp->CallAfter(std::move(task));
            } else {
                task();
            }
        });
    kernel_->SetOptions(BuildKernelOptions());
    kernel_->SetObserver(this);
    runtimeInitialized_ = true;

    if (!loaded.warning.empty()) {
        ShowMessage(wxString::FromUTF8("Mihomo settings: " + loaded.warning));
    }
    core::MarkStartup("kernel-session-initialized");

    if (settings_.autoStartKernel) {
        StartKernel();
    }
}

void AppController::ShutdownRuntime() {
    if (!kernel_) {
        runtimeInitialized_ = false;
        return;
    }
    kernel_->SetObserver(nullptr);
    kernel_->Shutdown();
    kernel_.reset();
    runtimeInitialized_ = false;
    core::MarkStartup("kernel-session-shutdown");
}

void AppController::StartKernel() {
    if (!kernel_) {
        ShowMessage("Mihomo runtime is not initialized yet.");
        return;
    }
    kernel_->Start();
}

void AppController::StopKernel() {
    if (kernel_) {
        kernel_->Stop();
    }
}

void AppController::RestartKernel() {
    if (!kernel_) {
        ShowMessage("Mihomo runtime is not initialized yet.");
        return;
    }
    kernel_->Restart();
}

runtime::KernelOptions AppController::BuildKernelOptions() const {
    runtime::KernelOptions options;
    options.executablePath = settings_.mihomoPath;
    options.workingDirectory = settingsDirectory_;
    options.configPath =
        PathToUtf8(PathFromUtf8(settingsDirectory_) / "clashyuka-runtime.yaml");
    options.controllerHost = settings_.controllerHost;
    options.controllerPort = settings_.controllerPort;
    options.mixedPort = settings_.mixedPort;
    options.secret = settings_.secret;
    return options;
}

bool AppController::SaveKernelSettings(const runtime::AppSettings& settings, wxString* error,
                                       bool* restartRequired) {
    if (error != nullptr) {
        error->clear();
    }
    if (restartRequired != nullptr) {
        *restartRequired = false;
    }

    const core::Result<void> valid = runtime::ValidateSettings(settings);
    if (!valid) {
        if (error != nullptr) {
            *error = wxString::FromUTF8(valid.error().message);
        }
        return false;
    }

    if (!settings.mihomoPath.empty()) {
        std::error_code fileError;
        if (!fs::is_regular_file(PathFromUtf8(settings.mihomoPath), fileError) || fileError) {
            if (error != nullptr) {
                *error = "The selected Mihomo executable does not exist.";
            }
            return false;
        }
    }

    if (settingsPath_.empty()) {
        settingsPath_ = DefaultSettingsPath();
    }
    const core::Result<void> saved = runtime::SaveSettings(settingsPath_, settings);
    if (!saved) {
        if (error != nullptr) {
            *error = wxString::FromUTF8(saved.error().message);
        }
        return false;
    }

    const bool active = IsSessionActive(kernel_.get());
    if (restartRequired != nullptr) {
        *restartRequired = active;
    }

    settings_ = settings;
    if (settingsDirectory_.empty()) {
        settingsDirectory_ = PathToUtf8(PathFromUtf8(settingsPath_).parent_path());
    }
    state_.SetKernelPath(settings_.mihomoPath);
    state_.SetApiAddress(FormatAddress(settings_.controllerHost, settings_.controllerPort));
    state_.SetKernelError("");
    if (kernel_) {
        kernel_->SetOptions(BuildKernelOptions());
    }
    return true;
}

void AppController::OnKernelSnapshot(const runtime::KernelSnapshot& snapshot) {
    if (!snapshot.apiAddress.empty()) {
        state_.SetApiAddress(snapshot.apiAddress);
    }
    if (!settings_.mihomoPath.empty()) {
        state_.SetKernelPath(settings_.mihomoPath);
    }

    state_.SetKernelState(MapKernelPhase(snapshot.phase));
    state_.SetApiState(MapApiPhase(snapshot.phase));
    if (!snapshot.version.empty()) {
        state_.SetMihomoVersion(snapshot.version);
    }

    switch (snapshot.phase) {
        case runtime::KernelPhase::Running:
            state_.SetProfile("Built-in Runtime Config", true);
            state_.SetKernelError("");
            break;
        case runtime::KernelPhase::Failed: {
            state_.SetProfile("", false);
            state_.SetTraffic(0.0, 0.0, false);
            state_.SetConnectionCount(0);
            const wxString friendly = KernelErrorText(snapshot.error.kind);
            state_.SetKernelError(friendly.utf8_string());
            if (snapshot.generation != lastShownFailure_) {
                lastShownFailure_ = snapshot.generation;
                std::fprintf(stderr, "[clashyuka] kernel failure: %s\n",
                             snapshot.error.message.c_str());
                if (!friendly.empty()) {
                    ShowMessage(friendly);
                }
            }
            break;
        }
        case runtime::KernelPhase::Stopped:
            state_.SetProfile("", false);
            state_.SetTraffic(0.0, 0.0, false);
            state_.SetConnectionCount(0);
            state_.SetKernelError("");
            break;
        case runtime::KernelPhase::Starting:
        case runtime::KernelPhase::WaitingForApi:
            state_.SetProfile("", false);
            state_.SetKernelError("");
            break;
        case runtime::KernelPhase::Stopping:
            break;
    }

    if (!snapshot.eventError.empty() &&
        snapshot.generation != lastShownEventWarning_) {
        lastShownEventWarning_ = snapshot.generation;
        std::fprintf(stderr, "[clashyuka] event stream warning: %s\n",
                     snapshot.eventError.c_str());
        ShowMessage("Live event stream is unavailable. Traffic data may be missing.");
    }
}

void AppController::OnKernelTraffic(double uploadBytesPerSecond,
                                    double downloadBytesPerSecond) {
    state_.SetTraffic(uploadBytesPerSecond, downloadBytesPerSecond, true);
}

void AppController::OnKernelConnections(int count) {
    state_.SetConnectionCount(count);
}

}  // namespace clashyuka::app
