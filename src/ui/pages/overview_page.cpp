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
    row->AddStretchSpacer();
    value = new TextBlock(parent, initial, TypeStyle::Body);
    row->Add(value, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(row, 0, wxEXPAND);
}

void RowGap(wxSizer* sizer, wxWindow* window) {
    sizer->AddSpacer(window->FromDIP(SpaceDp(Space::Sm)));
}

}  // namespace

OverviewPage::OverviewPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Overview) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this, "Mihomo is not running. The shell is ready for the kernel "
                                   "integration stage.",
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
    profileSizer->Add(pages::MakeSectionTitle(profileCard, "Active profile"), 0, wxEXPAND);
    pages::AddSpacer(profileSizer, profileCard, SpaceDp(Space::Md));
    AddRow(profileSizer, profileCard, "Profile", profileValue_, "No profile loaded");
    RowGap(profileSizer, profileCard);
    AddRow(profileSizer, profileCard, "Mode", modeValue_, "Rule");
    profileCard->SetContent(profileSizer, 20);
    statusRow->Add(coreCard, 1, wxEXPAND | wxRIGHT, FromDIP(SpaceDp(Space::Lg)));
    statusRow->Add(profileCard, 1, wxEXPAND);
    sizer->Add(statusRow, 0, wxEXPAND);

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
    profileValue_->SetText(state.profileLoaded ? wxString::FromUTF8(state.profileName)
                                               : wxString("No profile loaded"));
    modeValue_->SetText(app::ProxyModeText(state.proxyMode));
    uploadValue_->SetText(app::FormatSpeed(state.uploadBytesPerSecond, state.trafficAvailable));
    downloadValue_->SetText(
        app::FormatSpeed(state.downloadBytesPerSecond, state.trafficAvailable));
    trafficNote_->SetText(state.trafficAvailable
                              ? "Live traffic from the Mihomo event stream."
                              : "Traffic data is unavailable until the core is running (P2.2).");
}

}  // namespace clashyuka::ui
