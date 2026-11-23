#include "clashyuka/app/app_controller.h"

#include <cstdio>
#include <filesystem>
#include <utility>

#include <wx/app.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "clashyuka/app/app_text.h"
#include "clashyuka/app/archive_extractor.h"
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

constexpr char kInvalidDataDirectory[] = "CLASHYUKA_DATA_DIR must be an absolute directory path.";

std::string GetSettingsPath() {
    wxString directory;
    if (wxGetEnv("CLASHYUKA_DATA_DIR", &directory)) {
        const auto path = PathFromUtf8(directory.utf8_string());
        return path.is_absolute() ? PathToUtf8(path / "settings.json") : std::string{};
    }
    directory = wxStandardPaths::Get().GetUserDataDir();
    return PathToUtf8(PathFromUtf8(directory.utf8_string()) / "settings.json");
}

std::string FormatAddress(const std::string& host, int port) {
    if (host.find(':') != std::string::npos && !host.starts_with('[')) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

std::function<void(std::function<void()>)> MakeUiDispatcher() {
    return [](std::function<void()> task) {
        if (wxTheApp != nullptr) {
            wxTheApp->CallAfter(std::move(task));
        } else {
            task();
        }
    };
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
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { TickSubscriptions(); });
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

    if (settingsPath_.empty()) {
        settingsPath_ = GetSettingsPath();
    }
    if (settingsPath_.empty()) {
        state_.SetKernelError(kInvalidDataDirectory);
        ShowMessage(kInvalidDataDirectory);
        return;
    }
    const runtime::SettingsLoadResult loaded = runtime::LoadSettings(settingsPath_);
    settings_ = loaded.settings;
    settingsDirectory_ = PathToUtf8(PathFromUtf8(settingsPath_).parent_path());

    state_.SetKernelPath(settings_.mihomoPath);
    state_.SetApiAddress(FormatAddress(settings_.controllerHost, settings_.controllerPort));
    state_.SetKernelError("");

    profileStoreError_.clear();
    profileActionStatus_.clear();
    auto opened = runtime::ProfileStore::Open(PathFromUtf8(settingsDirectory_));
    if (opened) {
        profileStore_ = opened.Take();
        subscriptions_ = std::make_unique<runtime::SubscriptionService>(*profileStore_);
    } else {
        profileStoreError_ = "Could not open profiles: " + opened.error().message;
    }

    uiLifetime_ = std::make_shared<int>(0);
    kernel_ = std::make_unique<runtime::KernelSession>(
        runtime::CreateDefaultDriverFactory(), MakeUiDispatcher());
    kernel_->SetOptions(BuildKernelOptions());
    kernel_->SetObserver(this);
    systemProxy_ = platform::CreateSystemProxyController();
    state_.SetSystemProxy(settings_.enableSystemProxy, false, "");
    CreateInstaller();
    runtimeInitialized_ = true;
    PublishProfiles();
    if (subscriptions_ && subscriptions_->Available()) {
        subscriptionTimer_ = std::make_unique<wxTimer>(static_cast<wxEvtHandler*>(this));
        subscriptionTimer_->Start(250);
    }
    // Check a saved non-built-in selection even with auto-start disabled.
    if (!settings_.autoStartKernel && profileStore_ &&
        profileStore_->SelectedProfileId() != runtime::kBuiltInProfileId) {
        PrepareSelectedKernel();
    }

    if (!loaded.warning.empty()) {
        ShowMessage(wxString::FromUTF8("Mihomo settings: " + loaded.warning));
    }
    core::MarkStartup("kernel-session-initialized");

    if (settings_.autoStartKernel) {
        StartKernel();
    }
}

void AppController::ShutdownRuntime() {
    runtimeInitialized_ = false;
    subscriptionTimer_.reset();
    if (subscriptions_) {
        subscriptions_->Shutdown();
        subscriptions_.reset();
    }
    profileStore_.reset();
    addingSubscription_.reset();
    pendingProfileSwitch_.reset();
    profileRevisions_.clear();
    runningProfileId_.clear();
    launchProfileId_.clear();
    if (systemProxy_ && systemProxy_->Active()) {
        const auto disabled = systemProxy_->Disable();
        if (!disabled) {
            std::fprintf(stderr, "[clashyuka] system proxy shutdown: %s\n",
                         disabled.error().message.c_str());
        }
        systemProxyPort_ = 0;
    }
    systemProxy_.reset();
    StopInstallerThread();
    uiLifetime_.reset();
    installer_.reset();
    DestroyProxyService();
    if (!kernel_) {
        return;
    }
    kernel_->SetObserver(nullptr);
    kernel_->Shutdown();
    kernel_.reset();
    core::MarkStartup("kernel-session-shutdown");
}

void AppController::StartKernel() {
    if (!kernel_) {
        ShowMessage("Mihomo runtime is not initialized yet.");
        return;
    }
    const auto phase = kernel_->Phase();
    if (!pendingProfileSwitch_ &&
        (phase == runtime::KernelPhase::Stopped || phase == runtime::KernelPhase::Failed) &&
        PrepareSelectedKernel()) {
        kernel_->Start();
    }
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
    const auto current = kernel_->Snapshot();
    if (pendingProfileSwitch_ || current.profileSwitchInProgress ||
        current.phase == runtime::KernelPhase::Stopping) {
        return;
    }
    if (current.phase == runtime::KernelPhase::Running && profileStore_) {
        const auto selected = profileStore_->SelectedProfileId();
        if (selected != current.runningProfileId || profileRevisions_[selected] != runningRevision_) {
            ActivateProfile(selected);
            return;
        }
    }
    if (PrepareSelectedKernel()) {
        kernel_->Restart();
    }
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

core::Result<runtime::KernelProfile> AppController::LoadProfile(const std::string& id) const {
    if (id == runtime::kBuiltInProfileId) {
        return runtime::KernelProfile{id, std::nullopt};
    }
    auto bytes = profileStore_->LoadProfileBytes(id);
    if (!bytes) {
        return bytes.error();
    }
    return runtime::KernelProfile{id, bytes.Take()};
}

bool AppController::PrepareSelectedKernel() {
    if (!profileStore_) {
        state_.SetKernelError(profileStoreError_);
        ShowMessage(wxString::FromUTF8(profileStoreError_));
        return false;
    }
    const auto id = profileStore_->SelectedProfileId();
    auto profile = LoadProfile(id);
    if (!profile) {
        const auto message = "Could not load the next-start profile: " + profile.error().message +
                             ". Select Built-in in Profiles to recover.";
        state_.SetKernelError(message);
        ProfileMessage(wxString::FromUTF8(message));
        return false;
    }
    auto options = BuildKernelOptions();
    options.profile = profile.Take();
    launchProfileId_ = id;
    launchRevision_ = profileRevisions_[id];
    kernel_->SetOptions(std::move(options));
    return true;
}

void AppController::PublishProfiles() {
    if (!runtimeInitialized_) {
        return;
    }
    ProfilesSnapshot view;
    view.storeError = profileStoreError_;
    view.actionStatus = profileActionStatus_;
    view.runningProfileId = runningProfileId_;
    view.switching = pendingProfileSwitch_.has_value();
    view.addingSubscription = addingSubscription_.has_value();
    view.subscriptionsAvailable = subscriptions_ && subscriptions_->Available();
    if (profileStore_) {
        view.selectedProfileId = profileStore_->SelectedProfileId();
        for (const auto& profile : profileStore_->ListProfiles()) {
            view.rows.push_back({profile.id, profile.name, profile.kind, profile.refresh.outcome,
                                 profile.refresh.completedAt,
                                 subscriptions_ && subscriptions_->Busy(profile.id)});
        }
        view.runningCopyOutdated = !runningProfileId_.empty() &&
                                   profileRevisions_[runningProfileId_] != runningRevision_;
    }
    state_.SetProfiles(std::move(view));
}

void AppController::ProfileMessage(const wxString& message) {
    if (!runtimeInitialized_) {
        return;
    }
    profileActionStatus_ = message.utf8_string();
    PublishProfiles();
    ShowMessage(message);
}

void AppController::ImportProfile(const wxString& path) {
    if (!profileStore_) {
        return;
    }
    auto imported = profileStore_->ImportLocal(PathFromUtf8(path.utf8_string()));
    ProfileMessage(imported ? wxString("Profile imported. YAML checked. Mihomo checks it at activation.")
                            : wxString::FromUTF8(imported.error().message));
}

void AppController::RenameProfile(const std::string& id, const wxString& name) {
    if (!profileStore_) {
        return;
    }
    const auto renamed = profileStore_->RenameProfile(id, name.utf8_string());
    ProfileMessage(renamed ? wxString("Profile renamed.")
                           : wxString::FromUTF8(renamed.error().message));
}

bool AppController::CanDeleteProfile(const std::string& id) const {
    return profileStore_ && kernel_ && !pendingProfileSwitch_ &&
           id != runtime::kBuiltInProfileId && id != profileStore_->SelectedProfileId() &&
           id != kernel_->Snapshot().runningProfileId &&
           (!subscriptions_ || !subscriptions_->Busy(id));
}

void AppController::DeleteProfile(const std::string& id) {
    if (!CanDeleteProfile(id)) {
        ProfileMessage("A built-in, selected, running or busy profile cannot be deleted.");
        return;
    }
    if (!shell_ || !shell_->ShowConfirmDialog("Delete profile", "Delete this saved profile?", "Delete") ||
        !CanDeleteProfile(id)) {
        return;
    }
    const auto removed = profileStore_->DeleteProfile(id, kernel_->Snapshot().runningProfileId);
    if (removed) {
        profileRevisions_.erase(id);
    }
    ProfileMessage(removed ? wxString("Profile deleted.")
                           : wxString::FromUTF8(removed.error().message));
}

void AppController::PersistProfileSelection(const std::string& id) {
    const auto saved = profileStore_->SelectProfile(id);
    if (!saved) {
        ProfileMessage(wxString::FromUTF8("Could not save the next-start profile: " +
                                         saved.error().message + ". You can retry in Profiles."));
        return;
    }
    state_.SetKernelError("");
    ProfileMessage("Next-start profile saved.");
}

void AppController::ActivateProfile(const std::string& id) {
    if (!profileStore_ || !kernel_ || pendingProfileSwitch_) {
        return;
    }
    const auto current = kernel_->Snapshot();
    if (current.phase == runtime::KernelPhase::Running && current.runningProfileId == id &&
        profileStore_->SelectedProfileId() != id) {
        PersistProfileSelection(id);
        return;
    }
    if (current.phase == runtime::KernelPhase::Stopped || current.phase == runtime::KernelPhase::Failed) {
        auto profile = LoadProfile(id);
        if (!profile) {
            ProfileMessage(wxString::FromUTF8("Could not select profile: " + profile.error().message));
            return;
        }
        PersistProfileSelection(id);
        return;
    }
    if (current.phase != runtime::KernelPhase::Running || current.profileSwitchInProgress ||
        (current.runningProfileId == id && profileRevisions_[id] == runningRevision_)) {
        return;
    }
    if (!shell_ || !shell_->ShowConfirmDialog("Apply profile",
            "Mihomo will check the profile and restart the kernel if the check passes.", "Apply") ||
        !profileStore_ || !kernel_ || pendingProfileSwitch_) {
        return;
    }
    const auto confirmed = kernel_->Snapshot();
    if (confirmed.phase != runtime::KernelPhase::Running || confirmed.generation != current.generation) {
        ProfileMessage("The kernel changed during confirmation. Choose the profile again.");
        return;
    }
    auto profile = LoadProfile(id);
    if (!profile) {
        ProfileMessage(wxString::FromUTF8("Could not apply profile: " + profile.error().message));
        return;
    }
    switchRevision_ = profileRevisions_[id];
    pendingProfileSwitch_ = kernel_->SwitchProfile(profile.Take());
    ProfileMessage(pendingProfileSwitch_ ? "Checking and applying profile..."
                                         : "The kernel is no longer ready to switch profiles.");
}

void AppController::ConsumeProfileSwitch(const runtime::KernelSnapshot& snapshot) {
    if (!pendingProfileSwitch_ || !snapshot.profileSwitchResult ||
        snapshot.profileSwitchResult->requestId != *pendingProfileSwitch_) {
        return;
    }
    pendingProfileSwitch_.reset();
    const auto& result = *snapshot.profileSwitchResult;
    switch (result.outcome) {
        case runtime::ProfileSwitchOutcome::Succeeded:
            runningRevision_ = switchRevision_;
            PersistProfileSelection(result.newProfileId);
            break;
        case runtime::ProfileSwitchOutcome::Rejected:
            ProfileMessage(wxString::FromUTF8("Profile rejected: " + result.error.message));
            break;
        case runtime::ProfileSwitchOutcome::RolledBack:
            ProfileMessage(wxString::FromUTF8("The previous profile was restored. Switch failed: " +
                                              result.error.message));
            break;
        case runtime::ProfileSwitchOutcome::RollbackFailed:
            ProfileMessage("The new profile failed and Mihomo could not restore the previous one.");
            break;
        case runtime::ProfileSwitchOutcome::Cancelled:
            ProfileMessage("Profile switch cancelled.");
            break;
    }
}

std::optional<runtime::SubscriptionSettings> AppController::SubscriptionForEdit(const std::string& id) {
    if (!subscriptions_ || !subscriptions_->Available() || subscriptions_->Busy(id)) {
        return std::nullopt;
    }
    auto settings = profileStore_->GetSubscriptionSettings(id);
    if (!settings) {
        ProfileMessage(wxString::FromUTF8(settings.error().message));
        return std::nullopt;
    }
    return settings.Take();
}

void AppController::AddSubscription(const wxString& name, const runtime::SubscriptionSettings& settings) {
    if (!subscriptions_ || !subscriptions_->Available() || addingSubscription_) {
        return;
    }
    auto added = subscriptions_->Add(name.utf8_string(), settings, settings_.downloadProxy);
    if (added) {
        addingSubscription_ = added.value();
    }
    ProfileMessage(added ? wxString("Downloading subscription...")
                         : wxString::FromUTF8(added.error().message));
}

void AppController::EditSubscription(const std::string& id, const runtime::SubscriptionSettings& settings) {
    if (!subscriptions_ || !subscriptions_->Available()) {
        return;
    }
    const auto edited = subscriptions_->Edit(id, settings, settings_.downloadProxy);
    if (!edited) {
        ProfileMessage(wxString::FromUTF8(edited.error().message));
        return;
    }
    ProfileMessage(edited.value() ? "Downloading updated subscription..." : "Subscription settings saved.");
}

void AppController::RefreshSubscription(const std::string& id) {
    if (!subscriptions_ || !subscriptions_->Available()) {
        return;
    }
    const auto refreshing = subscriptions_->Refresh(id, settings_.downloadProxy);
    ProfileMessage(refreshing ? wxString("Refreshing subscription...")
                              : wxString::FromUTF8(refreshing.error().message));
}

void AppController::CancelSubscription(const std::string& id) {
    if (!subscriptions_) {
        return;
    }
    const bool cancelled = id.empty() ? (addingSubscription_ && subscriptions_->Cancel(*addingSubscription_))
                                      : subscriptions_->Cancel(id);
    if (cancelled) {
        ProfileMessage("Cancelling subscription download...");
    }
}

void AppController::TickSubscriptions() {
    if (!runtimeInitialized_ || !subscriptions_) {
        return;
    }
    const auto results = subscriptions_->Tick(settings_.downloadProxy);
    for (const auto& result : results) {
        if (addingSubscription_ == result.job) {
            addingSubscription_.reset();
        }
        if (result.outcome == runtime::ProfileRefreshOutcome::Succeeded) {
            ++profileRevisions_[result.profileId];
        }
    }
    PublishProfiles();
    for (const auto& result : results) {
        if (result.automatic) {
            continue;
        }
        if (result.error) {
            ProfileMessage(wxString::FromUTF8(result.error.message));
        } else if (result.operation == runtime::SubscriptionService::Operation::Add) {
            ProfileMessage("Subscription added. YAML checked. Mihomo checks it at activation.");
        } else {
            ProfileMessage("Subscription copy saved. Apply it in Profiles to update a running kernel.");
        }
    }
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
        settingsPath_ = GetSettingsPath();
    }
    if (settingsPath_.empty()) {
        if (error != nullptr) {
            *error = kInvalidDataDirectory;
        }
        return false;
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
        auto options = BuildKernelOptions();
        options.profile = kernel_->Options().profile;
        kernel_->SetOptions(std::move(options));
    }
    if (proxyService_) {
        proxyService_->SetTestSettings(settings_.proxyTestUrl, settings_.proxyTestTimeoutMs);
    }
    if (!installerBusy_) {
        CreateInstaller();
    }
    SyncSystemProxy();
    return true;
}

void AppController::OnKernelSnapshot(const runtime::KernelSnapshot& snapshot) {
    if (!runtimeInitialized_) {
        return;
    }
    const bool ready = snapshot.phase == runtime::KernelPhase::Running;
    runningProfileId_ = ready ? snapshot.runningProfileId : std::string{};
    if (ready && !launchProfileId_.empty() && launchProfileId_ == runningProfileId_) {
        runningRevision_ = launchRevision_;
        launchProfileId_.clear();
    } else if (snapshot.phase == runtime::KernelPhase::Failed) {
        launchProfileId_.clear();
    }
    if (proxyService_ && (!ready || proxyServiceGeneration_ != snapshot.generation)) {
        DestroyProxyService();
        state_.SetProxyCatalog(nullptr);
        state_.SetTraffic(0.0, 0.0, false);
        state_.SetConnectionCount(0);
    }
    if (!snapshot.apiAddress.empty()) {
        state_.SetApiAddress(snapshot.apiAddress);
    }
    if (!settings_.mihomoPath.empty()) {
        state_.SetKernelPath(settings_.mihomoPath);
    }

    state_.SetKernelState(MapKernelPhase(snapshot.phase));
    state_.SetApiState(MapApiPhase(snapshot.phase));
    state_.SetMihomoVersion(snapshot.version);
    SyncSystemProxy();

    switch (snapshot.phase) {
        case runtime::KernelPhase::Running:
            state_.SetKernelError("");
            CreateProxyService();
            if (proxyService_) {
                proxyServiceGeneration_ = snapshot.generation;
            }
            break;
        case runtime::KernelPhase::Failed: {
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
        case runtime::KernelPhase::Starting:
        case runtime::KernelPhase::WaitingForApi:
            state_.SetKernelError("");
            break;
        case runtime::KernelPhase::Stopping:
            break;
    }
    if (!ready) {
        state_.SetTraffic(0.0, 0.0, false);
        state_.SetConnectionCount(0);
    }
    if (snapshot.profileSwitchResult && pendingProfileSwitch_ == snapshot.profileSwitchResult->requestId &&
        snapshot.profileSwitchResult->outcome == runtime::ProfileSwitchOutcome::RollbackFailed) {
        state_.SetKernelError("Profile switch failed and Mihomo could not restore the previous kernel.");
    }
    ConsumeProfileSwitch(snapshot);
    PublishProfiles();

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

void AppController::CreateProxyService() {
    if (proxyService_ || !kernel_ || kernel_->Phase() != runtime::KernelPhase::Running) {
        return;
    }
    const mihomo::MihomoApiConfig config = kernel_->ApiConfig();
    if (config.port <= 0 || config.port > 65535) {
        return;
    }
    proxyService_ = std::make_unique<runtime::ProxyService>(
        runtime::CreateMihomoProxyApi(config), MakeUiDispatcher(), settings_.proxyTestUrl,
        settings_.proxyTestTimeoutMs);
    proxyService_->SetObserver(this);
    proxyService_->Refresh();
}

void AppController::DestroyProxyService() {
    if (!proxyService_) {
        return;
    }
    proxyService_->SetObserver(nullptr);
    proxyService_->Shutdown();
    proxyService_.reset();
}

void AppController::RefreshProxies() {
    if (!proxyService_) {
        ShowMessage("Start the Mihomo kernel to load proxy groups.");
        return;
    }
    proxyService_->Refresh();
}

void AppController::SelectProxy(std::string group, std::string target) {
    if (!proxyService_) {
        return;
    }
    proxyService_->Select(std::move(group), std::move(target));
}

void AppController::ClearProxySelection(std::string group) {
    if (!proxyService_) {
        return;
    }
    proxyService_->ClearSelection(std::move(group));
}

void AppController::TestProxyDelay(std::string name) {
    if (!proxyService_ || name.empty()) {
        return;
    }
    proxyService_->TestDelay(std::move(name));
}

void AppController::TestGroupDelays(std::vector<std::string> names) {
    if (!proxyService_) {
        return;
    }
    proxyService_->TestGroup(std::move(names));
}

void AppController::CancelProxyTests() {
    if (proxyService_) {
        proxyService_->CancelTests();
    }
}

void AppController::SyncSystemProxy() {
    if (!systemProxy_) {
        return;
    }
    const auto session = kernel_ ? kernel_->Snapshot() : runtime::KernelSnapshot{};
    const bool running = session.phase == runtime::KernelPhase::Running;
    const int port = session.mixedPort;
    const bool shouldApply = settings_.enableSystemProxy && running && port > 0 && port <= 65535;
    if (!shouldApply) {
        if (systemProxy_->Active()) {
            const core::Result<void> disabled = systemProxy_->Disable();
            if (!disabled) {
                ShowMessage(wxString::FromUTF8("System proxy: " + disabled.error().message));
            }
        }
        if (!systemProxy_->Active()) {
            systemProxyPort_ = 0;
        }
        state_.SetSystemProxy(settings_.enableSystemProxy, systemProxy_->Active(),
                              systemProxy_->ActiveServer());
        if (settings_.enableSystemProxy && running) {
            ShowMessage("System proxy requires an enabled Mihomo mixed port.");
        }
        return;
    }

    if (systemProxy_->Active() && systemProxyPort_ == port) {
        state_.SetSystemProxy(true, true, systemProxy_->ActiveServer());
        return;
    }
    if (systemProxy_->Active()) {
        const auto disabled = systemProxy_->Disable();
        if (!disabled) {
            ShowMessage(wxString::FromUTF8("System proxy: " + disabled.error().message));
            return;
        }
        systemProxyPort_ = 0;
    }
    const core::Result<void> enabled = systemProxy_->Enable(
        "127.0.0.1", port, platform::DefaultSystemProxyBypass());
    if (!enabled) {
        state_.SetSystemProxy(settings_.enableSystemProxy, systemProxy_->Active(),
                              systemProxy_->ActiveServer());
        ShowMessage(wxString::FromUTF8("System proxy: " + enabled.error().message));
        return;
    }
    systemProxyPort_ = port;
    state_.SetSystemProxy(true, true, systemProxy_->ActiveServer());
}

void AppController::SetSystemProxyEnabled(bool enabled) {
    if (settings_.enableSystemProxy == enabled) {
        SyncSystemProxy();
        return;
    }
    runtime::AppSettings next = settings_;
    next.enableSystemProxy = enabled;
    wxString error;
    if (!SaveKernelSettings(next, &error, nullptr)) {
        ShowMessage("Could not save the system proxy preference: " + error);
        return;
    }
    const bool active = state_.Get().systemProxyActive;
    if (enabled && active) {
        ShowMessage("System proxy enabled.");
    } else if (enabled && (!kernel_ || kernel_->Phase() != runtime::KernelPhase::Running)) {
        ShowMessage("System proxy will apply when the kernel runs.");
    } else if (!enabled && !active) {
        ShowMessage("System proxy disabled.");
    }
}

void AppController::OnProxyCatalog(const std::shared_ptr<const runtime::ProxyCatalog>& catalog) {
    state_.SetProxyCatalog(catalog);
    if (catalog && !catalog->error.empty() &&
        catalog->generation != lastShownProxyError_) {
        lastShownProxyError_ = catalog->generation;
        ShowMessage(wxString::FromUTF8("Proxy data: " + catalog->error));
    }
}

void AppController::CreateInstaller() {
    if (installerBusy_) {
        return;
    }
    installRoot_ = settings_.kernelInstallDir.empty()
                       ? runtime::DefaultKernelInstallRoot(settingsDirectory_)
                       : settings_.kernelInstallDir;
    runtime::KernelInstallOptions options;
    options.installRoot = installRoot_;
    options.downloadProxy = settings_.downloadProxy;
    installer_ = std::make_unique<runtime::KernelInstaller>(
        runtime::CreatePlatformKernelTransport(), app::CreateWxArchiveExtractor(),
        std::move(options));
}

void AppController::PostToUi(std::function<void()> task) {
    if (!task) {
        return;
    }
    const std::weak_ptr<int> lifetime = uiLifetime_;
    MakeUiDispatcher()([lifetime, task = std::move(task)] {
        if (!lifetime.expired()) {
            task();
        }
    });
}

void AppController::PublishKernelUpdate(
    const std::function<void(runtime::KernelUpdateSnapshot&)>& mutate) {
    std::shared_ptr<runtime::KernelUpdateSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(installerMutex_);
        if (!updateSnapshot_) {
            updateSnapshot_ = std::make_shared<runtime::KernelUpdateSnapshot>();
        }
        snapshot = std::make_shared<runtime::KernelUpdateSnapshot>(*updateSnapshot_);
        mutate(*snapshot);
        updateSnapshot_ = snapshot;
    }
    PostToUi([this, snapshot] { state_.SetKernelUpdate(snapshot); });
}

void AppController::StartInstallerThread(std::function<void()> task) {
    bool expected = false;
    if (!installerBusy_.compare_exchange_strong(expected, true)) {
        return;
    }
    installerCancel_.store(false);
    if (installerThread_.joinable()) {
        installerThread_.join();
    }
    installerThread_ = std::thread([this, task = std::move(task)] {
        try {
            task();
        } catch (const std::exception& error) {
            PublishKernelUpdate([&error](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Failed;
                snapshot.checking = false;
                snapshot.error = std::string("kernel installer error: ") + error.what();
                snapshot.message = snapshot.error;
            });
        } catch (...) {
            PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Failed;
                snapshot.checking = false;
                snapshot.error = "kernel installer error";
                snapshot.message = snapshot.error;
            });
        }
        installerBusy_.store(false);
    });
}

void AppController::StopInstallerThread() {
    installerCancel_.store(true);
    if (installerThread_.joinable()) {
        installerThread_.join();
    }
    installerBusy_.store(false);
}

void AppController::CheckKernelUpdate() {
    if (installerBusy_.load()) {
        return;
    }
    if (!installer_) {
        CreateInstaller();
    }
    if (!installer_) {
        ShowMessage("Kernel installer is not initialized.");
        return;
    }
    PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
        snapshot = runtime::KernelUpdateSnapshot{};
        snapshot.stage = runtime::InstallStage::Checking;
        snapshot.checking = true;
        snapshot.message = "Querying the latest Mihomo release";
    });

    runtime::KernelInstaller* installer = installer_.get();
    StartInstallerThread([this, installer] {
        auto progress = [this](const runtime::InstallProgress& value) {
            PublishKernelUpdate([&value](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = value.stage;
                snapshot.downloaded = value.downloaded;
                snapshot.total = value.total;
                snapshot.message = value.message;
                snapshot.checking = value.stage == runtime::InstallStage::Checking;
            });
        };

        core::Result<runtime::KernelRelease> release =
            installer->FetchLatest(installerCancel_, progress);
        if (!release) {
            PublishKernelUpdate([&release](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Failed;
                snapshot.checking = false;
                snapshot.error = release.error().message;
                snapshot.message = snapshot.error;
            });
            return;
        }
        core::Result<runtime::KernelAsset> asset =
            runtime::SelectKernelAsset(release.value(), runtime::CurrentReleasePlatform());
        if (!asset) {
            PublishKernelUpdate([&asset](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Failed;
                snapshot.checking = false;
                snapshot.error = asset.error().message;
                snapshot.message = snapshot.error;
            });
            return;
        }

        const runtime::KernelRelease selectedRelease = release.value();
        const runtime::KernelAsset selectedAsset = asset.value();
        {
            std::lock_guard<std::mutex> lock(installerMutex_);
            latestRelease_ = selectedRelease;
            latestAsset_ = selectedAsset;
            latestReleaseReady_ = true;
        }
        PublishKernelUpdate([&selectedRelease, &selectedAsset](
                                runtime::KernelUpdateSnapshot& snapshot) {
            snapshot = runtime::KernelUpdateSnapshot{};
            snapshot.stage = runtime::InstallStage::Idle;
            snapshot.releaseReady = true;
            snapshot.releaseTag = selectedRelease.tag;
            snapshot.releasePublishedAt = selectedRelease.publishedAt;
            snapshot.releaseUrl = selectedRelease.htmlUrl;
            snapshot.assetName = selectedAsset.name;
            snapshot.assetSize = selectedAsset.size;
            snapshot.assetDigest = runtime::NormalizeSha256(selectedAsset.digest);
            snapshot.message = "Latest release " + selectedRelease.tag;
        });
    });
}

void AppController::DownloadKernelUpdate(bool allowUnverifiedChecksum) {
    if (installerBusy_.load()) {
        return;
    }
    if (!installer_) {
        CreateInstaller();
    }
    if (!installer_) {
        ShowMessage("Kernel installer is not initialized.");
        return;
    }

    bool missingDigest = false;
    if (!allowUnverifiedChecksum) {
        std::lock_guard<std::mutex> lock(installerMutex_);
        missingDigest = latestReleaseReady_ &&
                        runtime::NormalizeSha256(latestAsset_.digest).empty();
    }
    if (missingDigest) {
        if (shell_ != nullptr &&
            shell_->ShowConfirmDialog(
                "Unverified kernel",
                "This release does not publish a SHA256 digest. Continue without official "
                "verification?",
                "Download anyway")) {
            DownloadKernelUpdate(true);
        }
        return;
    }

    PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
        snapshot.stage = runtime::InstallStage::Downloading;
        snapshot.checking = false;
        snapshot.error.clear();
        snapshot.installedPath.clear();
        snapshot.installedVersion.clear();
        snapshot.message = "Preparing the download";
    });

    runtime::KernelInstaller* installer = installer_.get();
    StartInstallerThread([this, installer, allowUnverifiedChecksum] {
        runtime::KernelRelease release;
        runtime::KernelAsset asset;
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(installerMutex_);
            ready = latestReleaseReady_;
            release = latestRelease_;
            asset = latestAsset_;
        }

        auto publishRelease = [this](const runtime::KernelRelease& value,
                                     const runtime::KernelAsset& selected) {
            PublishKernelUpdate([&value, &selected](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.releaseReady = true;
                snapshot.releaseTag = value.tag;
                snapshot.releasePublishedAt = value.publishedAt;
                snapshot.releaseUrl = value.htmlUrl;
                snapshot.assetName = selected.name;
                snapshot.assetSize = selected.size;
                snapshot.assetDigest = runtime::NormalizeSha256(selected.digest);
            });
        };

        if (!ready) {
            auto fetched = installer->FetchLatest(
                installerCancel_, [this](const runtime::InstallProgress& value) {
                    PublishKernelUpdate([&value](runtime::KernelUpdateSnapshot& snapshot) {
                        snapshot.stage = value.stage;
                        snapshot.message = value.message;
                        snapshot.checking = value.stage == runtime::InstallStage::Checking;
                    });
                });
            if (!fetched) {
                PublishKernelUpdate([&fetched](runtime::KernelUpdateSnapshot& snapshot) {
                    snapshot.stage = runtime::InstallStage::Failed;
                    snapshot.error = fetched.error().message;
                    snapshot.message = snapshot.error;
                });
                return;
            }
            auto selected =
                runtime::SelectKernelAsset(fetched.value(), runtime::CurrentReleasePlatform());
            if (!selected) {
                PublishKernelUpdate([&selected](runtime::KernelUpdateSnapshot& snapshot) {
                    snapshot.stage = runtime::InstallStage::Failed;
                    snapshot.error = selected.error().message;
                    snapshot.message = snapshot.error;
                });
                return;
            }
            release = fetched.value();
            asset = selected.value();
            {
                std::lock_guard<std::mutex> lock(installerMutex_);
                latestRelease_ = release;
                latestAsset_ = asset;
                latestReleaseReady_ = true;
            }
            publishRelease(release, asset);
        }

        if (installerCancel_.load()) {
            PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Cancelled;
                snapshot.message = "Download cancelled";
            });
            return;
        }
        if (!allowUnverifiedChecksum && runtime::NormalizeSha256(asset.digest).empty()) {
            PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = runtime::InstallStage::Failed;
                snapshot.error =
                    "this release does not provide a verifiable SHA256 digest";
                snapshot.message = snapshot.error;
            });
            installerBusy_.store(false);
            PostToUi([this] {
                if (shell_ != nullptr &&
                    shell_->ShowConfirmDialog(
                        "Unverified kernel",
                        "This release does not publish a SHA256 digest. Continue without "
                        "official verification?",
                        "Download anyway")) {
                    DownloadKernelUpdate(true);
                }
            });
            return;
        }

        installer->SetAllowUnverifiedChecksum(allowUnverifiedChecksum);
        auto lastPublished = std::make_shared<std::uint64_t>(0);
        auto progress = [this, lastPublished](const runtime::InstallProgress& value) {
            if (value.stage == runtime::InstallStage::Downloading &&
                value.downloaded < *lastPublished + 256 * 1024 &&
                (value.total == 0 || value.downloaded < value.total)) {
                return;
            }
            *lastPublished = value.downloaded;
            PublishKernelUpdate([&value](runtime::KernelUpdateSnapshot& snapshot) {
                snapshot.stage = value.stage;
                snapshot.downloaded = value.downloaded;
                snapshot.total = value.total;
                snapshot.message = value.message;
                snapshot.checking = false;
            });
        };

        core::Result<runtime::KernelInstallResult> installed =
            installer->Install(release, asset, installerCancel_, progress);
        if (!installed) {
            PublishKernelUpdate([&installed](runtime::KernelUpdateSnapshot& snapshot) {
                if (installed.error().kind == core::ErrorKind::Cancelled) {
                    snapshot.stage = runtime::InstallStage::Cancelled;
                    snapshot.message = "Download cancelled";
                } else {
                    snapshot.stage = runtime::InstallStage::Failed;
                    snapshot.error = installed.error().message;
                    snapshot.message = snapshot.error;
                }
            });
            if (installed.error().kind == core::ErrorKind::ChecksumMissing &&
                !allowUnverifiedChecksum) {
                installerBusy_.store(false);
                PostToUi([this] {
                    if (shell_ != nullptr &&
                        shell_->ShowConfirmDialog(
                            "Unverified kernel",
                            "This release does not publish a SHA256 digest. Continue without "
                            "official verification?",
                            "Download anyway")) {
                        DownloadKernelUpdate(true);
                    }
                });
            }
            return;
        }

        const runtime::KernelInstallResult result = installed.value();
        PublishKernelUpdate([&result](runtime::KernelUpdateSnapshot& snapshot) {
            snapshot.stage = runtime::InstallStage::Completed;
            snapshot.downloaded = snapshot.total;
            snapshot.installedVersion = result.version;
            snapshot.installedPath = result.executablePath;
            snapshot.installedChecksumVerified = result.checksumVerified;
            snapshot.message = result.checksumVerified
                                   ? "Installed and verified " + result.version
                                   : "Installed " + result.version + " (unverified checksum)";
            snapshot.error.clear();
        });
        PostToUi([this, result] {
            const wxString body =
                wxString::FromUTF8("Mihomo " + result.version +
                                   " was installed.\n\nUse it for the next kernel start?");
            if (shell_ != nullptr &&
                shell_->ShowConfirmDialog("Kernel installed", body, "Use this kernel")) {
                UseInstalledKernel();
            } else {
                ShowMessage("Kernel installed. You can switch kernels later in Settings.");
            }
        });
    });
}

void AppController::CancelKernelUpdate() {
    installerCancel_.store(true);
    PublishKernelUpdate([](runtime::KernelUpdateSnapshot& snapshot) {
        snapshot.message = "Cancelling the download";
    });
}

void AppController::UseInstalledKernel() {
    const runtime::KernelUpdateSnapshot* update = state_.Get().kernelUpdate.get();
    if (update == nullptr || update->installedPath.empty()) {
        ShowMessage("No installed kernel is available to switch to.");
        return;
    }
    runtime::AppSettings next = settings_;
    next.mihomoPath = update->installedPath;
    wxString error;
    bool restartRequired = false;
    if (!SaveKernelSettings(next, &error, &restartRequired)) {
        ShowMessage("Could not switch the kernel: " + error);
        return;
    }
    ShowMessage(restartRequired
                    ? "Kernel path updated. Restart the running kernel to use it."
                    : "Kernel path updated.");
}

}  // namespace clashyuka::app
