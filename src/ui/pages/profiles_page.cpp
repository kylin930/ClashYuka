#include "clashyuka/ui/pages/profiles_page.h"

#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {

ProfilesPage::ProfilesPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Profiles) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             "Subscription download, file import and configuration parsing "
                             "are not implemented in this build.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* currentCard = pages::MakeCard(this);
    auto* currentSizer = new wxBoxSizer(wxVERTICAL);
    currentSizer->Add(pages::MakeSectionTitle(currentCard, "Current profile"), 0, wxEXPAND);
    pages::AddSpacer(currentSizer, currentCard, SpaceDp(Space::Md));

    auto* nameRow = new wxBoxSizer(wxHORIZONTAL);
    nameRow->Add(new TextBlock(currentCard, "Profile", TypeStyle::Label, 170), 0,
                 wxALIGN_CENTER_VERTICAL);
    nameRow->AddStretchSpacer();
    profileValue_ = new TextBlock(currentCard, "No profile loaded", TypeStyle::Body);
    nameRow->Add(profileValue_, 0, wxALIGN_CENTER_VERTICAL);
    currentSizer->Add(nameRow, 0, wxEXPAND);
    pages::AddSpacer(currentSizer, currentCard, SpaceDp(Space::Sm));

    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusRow->Add(new TextBlock(currentCard, "Status", TypeStyle::Label, 170), 0,
                   wxALIGN_CENTER_VERTICAL);
    statusRow->AddStretchSpacer();
    statusValue_ = new TextBlock(currentCard, "Not loaded", TypeStyle::Body, 180);
    statusRow->Add(statusValue_, 0, wxALIGN_CENTER_VERTICAL);
    currentSizer->Add(statusRow, 0, wxEXPAND);
    pages::AddSpacer(currentSizer, currentCard, SpaceDp(Space::Lg));

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    auto* importButton = new Button(currentCard, "Import file", ButtonVariant::FilledTonal);
    importButton->SetOnClick([this] {
        Controller().ShowNotImplemented(
            "Profile import", "YAML import and validation will be added with the profile stage.");
    });
    auto* updateButton = new Button(currentCard, "Update", ButtonVariant::Outlined);
    updateButton->SetOnClick([this] {
        Controller().ShowNotImplemented(
            "Subscription update", "Remote subscriptions are not downloaded in this build.");
    });
    actions->Add(importButton, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    actions->Add(updateButton, 0);
    currentSizer->Add(actions, 0, wxEXPAND);
    currentCard->SetContent(currentSizer, 20);
    sizer->Add(currentCard, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* listTitle = new TextBlock(this, "Profiles", TypeStyle::Title);
    sizer->Add(listTitle, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));

    auto* direct = new ListItem(this);
    direct->SetTitle("Direct configuration");
    direct->SetSubtitle(wxString::FromUTF8("Built-in · Not loaded"));
    direct->SetTrailingText("Demo");
    direct->SetLeadingIcon(IconId::Home);
    direct->SetOnClick([this] {
        Controller().ShowNotImplemented(
            "Profile switching", "Only one profile exists in this shell; switching is disabled.");
    });
    sizer->Add(direct, 0, wxEXPAND);

    auto* demo = new ListItem(this);
    demo->SetTitle("Demo subscription");
    demo->SetSubtitle(wxString::FromUTF8("Example data · Not available"));
    demo->SetTrailingText("Demo");
    demo->SetLeadingIcon(IconId::Info);
    demo->SetShowDivider(true);
    demo->SetOnClick([this] {
        Controller().ShowNotImplemented(
            "Demo subscription", "This entry only illustrates the profile list layout.");
    });
    sizer->Add(demo, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* detailsTitle = new TextBlock(this, "Profile details", TypeStyle::Title);
    sizer->Add(detailsTitle, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));
    detailsValue_ = new TextBlock(this,
                                  "Rules, proxies and providers will be summarised here after "
                                  "a profile is loaded.",
                                  TypeStyle::Body, 170);
    sizer->Add(detailsValue_, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void ProfilesPage::ApplyState(const app::AppStateSnapshot& state) {
    if (profileValue_ == nullptr) {
        return;
    }
    profileValue_->SetText(state.profileLoaded ? wxString::FromUTF8(state.profileName)
                                               : wxString("No profile loaded"));
    statusValue_->SetText(state.profileLoaded ? "Loaded" : "Not loaded");
    detailsValue_->SetText(
        state.profileLoaded
            ? "Profile metadata is available. Detailed summaries arrive with the profile stage."
            : "Rules, proxies and providers will be summarised here after a profile is loaded.");
}

}  // namespace clashyuka::ui
