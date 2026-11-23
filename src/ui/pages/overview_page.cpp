#include "clashyuka/ui/pages/overview_page.h"

#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/app/app_text.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

void AddRow(wxSizer* sizer, wxWindow* parent, const wxString& label, TextBlock*& value,
            const wxString& initial) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new TextBlock(parent, label, TypeStyle::Label, 170), 0, wxALIGN_CENTER_VERTICAL);
    value = new TextBlock(parent, initial, TypeStyle::Body);
    value->SetAlignment(wxALIGN_RIGHT);
    row->Add(value, 1, wxEXPAND);
    sizer->Add(row, 0, wxEXPAND);
}

void RowGap(wxSizer* sizer, wxWindow* window) {
    sizer->AddSpacer(window->FromDIP(SpaceDp(Space::Sm)));
}

}  // namespace

OverviewPage::OverviewPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Overview) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this, "Start the Mihomo core to stream live status and traffic. "
                                   "The shell stays usable while the kernel starts.",
                             TypeStyle::Body, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);

    auto* coreCard = pages::MakeCard(this);
    auto* coreSizer = new wxBoxSizer(wxVERTICAL);
    coreSizer->Add(pages::MakeSectionTitle(coreCard, "Mihomo core"), 0, wxEXPAND);
    pages::AddSpacer(coreSizer, coreCard, SpaceDp(Space::Md));
    AddRow(coreSizer, coreCard, "Kernel", kernelValue_, "Not started");
    RowGap(coreSizer, coreCard);
    AddRow(coreSizer, coreCard, "API", apiValue_, "Disconnected");
    RowGap(coreSizer, coreCard);
    AddRow(coreSizer, coreCard, "Version", versionValue_, "Unknown");
    coreCard->SetContent(coreSizer, 20);

    auto* profileCard = pages::MakeCard(this);
    auto* profileSizer = new wxBoxSizer(wxVERTICAL);
    profileSizer->Add(pages::MakeSectionTitle(profileCard, "Profiles"), 0, wxEXPAND);
    pages::AddSpacer(profileSizer, profileCard, SpaceDp(Space::Md));
    AddRow(profileSizer, profileCard, "Running", profileValue_, "None");
    RowGap(profileSizer, profileCard);
    AddRow(profileSizer, profileCard, "Next start", nextProfileValue_, "Unavailable");
    RowGap(profileSizer, profileCard);
    AddRow(profileSizer, profileCard, "Mode", modeValue_, "Rule");
    profileCard->SetContent(profileSizer, 20);
    statusRow->Add(coreCard, 1, wxEXPAND | wxRIGHT, FromDIP(SpaceDp(Space::Lg)));
    statusRow->Add(profileCard, 1, wxEXPAND);
    sizer->Add(statusRow, 0, wxEXPAND);

    // Full-width so a long failure message can never squeeze the two cards and
    // clip the Active profile value.
    errorLabel_ = new TextBlock(this, "", TypeStyle::Label, 190);
    errorLabel_->Hide();
    sizer->Add(errorLabel_, 0, wxEXPAND | wxTOP, FromDIP(SpaceDp(Space::Sm)));

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* trafficCard = pages::MakeCard(this);
    auto* trafficSizer = new wxBoxSizer(wxVERTICAL);
    trafficSizer->Add(pages::MakeSectionTitle(trafficCard, "Traffic"), 0, wxEXPAND);
    pages::AddSpacer(trafficSizer, trafficCard, SpaceDp(Space::Md));
    AddRow(trafficSizer, trafficCard, "Upload", uploadValue_, wxString::FromUTF8("—"));
    RowGap(trafficSizer, trafficCard);
    AddRow(trafficSizer, trafficCard, "Download", downloadValue_, wxString::FromUTF8("—"));
    pages::AddSpacer(trafficSizer, trafficCard, SpaceDp(Space::Md));
    trafficNote_ = new TextBlock(trafficCard,
                                 "Traffic data is unavailable until the core is running (P2.2).",
                                 TypeStyle::Label, 160);
    trafficSizer->Add(trafficNote_, 0, wxEXPAND);
    trafficCard->SetContent(trafficSizer, 20);
    sizer->Add(trafficCard, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    startButton_ = new Button(this, "Start", ButtonVariant::Filled);
    startButton_->SetOnClick([this] { Controller().StartKernel(); });
    stopButton_ = new Button(this, "Stop", ButtonVariant::Outlined);
    stopButton_->SetOnClick([this] { Controller().StopKernel(); });
    restartButton_ = new Button(this, "Restart", ButtonVariant::Text);
    restartButton_->SetOnClick([this] { Controller().RestartKernel(); });
    actions->Add(startButton_, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    actions->Add(stopButton_, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    actions->Add(restartButton_, 0, wxRIGHT, FromDIP(SpaceDp(Space::Xl)));
    auto* proxies = new Button(this, "Open proxies", ButtonVariant::FilledTonal);
    proxies->SetOnClick([this] { Controller().Navigate(app::PageId::Proxies); });
    auto* profiles = new Button(this, "Profiles", ButtonVariant::Outlined);
    profiles->SetOnClick([this] { Controller().Navigate(app::PageId::Profiles); });
    auto* settings = new Button(this, "Settings", ButtonVariant::Text);
    settings->SetOnClick([this] { Controller().Navigate(app::PageId::Settings); });
    actions->Add(proxies, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    actions->Add(profiles, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    actions->Add(settings, 0);
    sizer->Add(actions, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* activityCard = pages::MakeCard(this);
    auto* activitySizer = new wxBoxSizer(wxVERTICAL);
    activitySizer->Add(pages::MakeSectionTitle(activityCard, "Recent activity"), 0, wxEXPAND);
    pages::AddSpacer(activitySizer, activityCard, SpaceDp(Space::Sm));
    activitySizer->Add(new TextBlock(activityCard, "No recent activity", TypeStyle::Body, 200), 0,
                       wxEXPAND);
    activitySizer->Add(new TextBlock(activityCard,
                                     "Kernel events will appear here once Mihomo is connected.",
                                     TypeStyle::Label, 160),
                       0, wxEXPAND);
    activityCard->SetContent(activitySizer, 20);
    sizer->Add(activityCard, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void OverviewPage::ApplyState(const app::AppStateSnapshot& state) {
    if (kernelValue_ == nullptr) {
        return;
    }
    kernelValue_->SetText(app::KernelStateText(state.kernel));
    apiValue_->SetText(app::ApiStateText(state.api));
    versionValue_->SetText(app::FormatVersion(state.mihomoVersion));
    const auto& profiles = state.profiles;
    wxString runningProfile = "None";
    wxString nextProfile = "Unavailable";
    if (profiles) {
        if (!profiles->runningProfileId.empty()) {
            runningProfile = profiles->NameFor(profiles->runningProfileId, "Unknown");
            if (profiles->runningCopyOutdated) {
                runningProfile += " (update ready)";
            }
        }
        if (!profiles->selectedProfileId.empty()) {
            nextProfile = profiles->NameFor(profiles->selectedProfileId, "Missing profile");
        }
    }
    profileValue_->SetText(runningProfile);
    nextProfileValue_->SetText(nextProfile);
    modeValue_->SetText(app::ProxyModeText(state.proxyMode));
    uploadValue_->SetText(app::FormatSpeed(state.uploadBytesPerSecond, state.trafficAvailable));
    downloadValue_->SetText(
        app::FormatSpeed(state.downloadBytesPerSecond, state.trafficAvailable));
    const bool running = state.kernel == app::KernelState::Running;
    trafficNote_->SetText(!running
                              ? "Start the core to stream live traffic."
                              : (state.trafficAvailable
                                     ? "Live traffic from the Mihomo event stream."
                                     : "Waiting for the traffic event stream..."));

    const bool canLaunch = profiles && profiles->storeError.empty() && !profiles->switching;
    startButton_->Enable(canLaunch && (state.kernel == app::KernelState::NotStarted ||
                                      state.kernel == app::KernelState::Error));
    stopButton_->Enable(state.kernel == app::KernelState::Starting ||
                        state.kernel == app::KernelState::WaitingForApi ||
                        state.kernel == app::KernelState::Running);
    restartButton_->Enable(canLaunch && (state.kernel == app::KernelState::NotStarted ||
                                        state.kernel == app::KernelState::Running ||
                                        state.kernel == app::KernelState::Error));

    const bool hasError = !state.kernelError.empty();
    errorLabel_->SetText(wxString::FromUTF8(state.kernelError));
    if (errorLabel_->IsShown() != hasError) {
        errorLabel_->Show(hasError);
    }

    const std::string signature =
        std::to_string(static_cast<int>(state.kernel)) + "|" +
        std::to_string(static_cast<int>(state.api)) + "|" + state.mihomoVersion + "|" +
        runningProfile.utf8_string() + "|" + nextProfile.utf8_string() + "|" +
        std::to_string(static_cast<int>(state.proxyMode)) + "|" + state.kernelError;
    if (signature != layoutSignature_) {
        layoutSignature_ = signature;
        RequestRelayout();
    }
}

}  // namespace clashyuka::ui
