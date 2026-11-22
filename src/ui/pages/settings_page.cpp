#include "clashyuka/ui/pages/settings_page.h"

#include <utility>

#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/core/app_info.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/switch.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

void AddSettingRow(wxSizer* sizer, wxWindow* parent, const wxString& label, wxWindow* control) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new TextBlock(parent, label, TypeStyle::Body), 0, wxALIGN_CENTER_VERTICAL);
    row->AddStretchSpacer();
    row->Add(control, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(row, 0, wxEXPAND);
}

void AddParagraph(wxSizer* sizer, wxWindow* parent, const wxString& text, int alpha = 180) {
    sizer->Add(new TextBlock(parent, text, TypeStyle::Label, alpha), 0, wxEXPAND);
}

ListItem* MakeDisabledItem(wxWindow* parent, const wxString& title, const wxString& subtitle,
                           const wxString& trailing, IconId icon = IconId::Info) {
    auto* item = new ListItem(parent);
    item->SetTitle(title);
    item->SetSubtitle(subtitle);
    item->SetTrailingText(trailing);
    item->SetLeadingIcon(icon);
    item->SetShowChevron(false);
    item->Enable(false);
    return item;
}

SurfaceCard* MakeSection(wxWindow* parent, const wxString& title, wxSizer** content) {
    auto* card = pages::MakeCard(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(pages::MakeSectionTitle(card, title), 0, wxEXPAND);
    pages::AddSpacer(sizer, card, SpaceDp(Space::Md));
    *content = sizer;
    card->SetContent(sizer, 20);
    return card;
}

}  // namespace

SettingsPage::SettingsPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Settings) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             "Appearance settings apply immediately. The other groups are "
                             "placeholders for later stages.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    // --- Appearance --------------------------------------------------------
    wxSizer* appearanceSizer = nullptr;
    auto* appearance = MakeSection(this, "Appearance", &appearanceSizer);

    auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
    modeRow->Add(new TextBlock(appearance, "Theme mode", TypeStyle::Body), 0,
                 wxALIGN_CENTER_VERTICAL);
    modeRow->AddStretchSpacer();
    lightButton_ = new Button(appearance, "Light", ButtonVariant::Text);
    darkButton_ = new Button(appearance, "Dark", ButtonVariant::Text);
    lightButton_->SetOnClick([this] { Controller().SetDarkMode(false); });
    darkButton_->SetOnClick([this] { Controller().SetDarkMode(true); });
    modeRow->Add(lightButton_, 0, wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    modeRow->Add(darkButton_, 0);
    appearanceSizer->Add(modeRow, 0, wxEXPAND);
    pages::AddSpacer(appearanceSizer, appearance, SpaceDp(Space::Md));

    auto* colorRow = new wxBoxSizer(wxHORIZONTAL);
    colorRow->Add(new TextBlock(appearance, "Color", TypeStyle::Body), 0,
                  wxALIGN_CENTER_VERTICAL);
    colorRow->AddStretchSpacer();
    const std::pair<ColorTheme, const char*> themes[] = {
        {ColorTheme::Purple, "Purple"},
        {ColorTheme::Blue, "Blue"},
        {ColorTheme::Teal, "Teal"},
        {ColorTheme::Orange, "Orange"},
    };
    for (std::size_t i = 0; i < 4; ++i) {
        auto* button = new Button(appearance, themes[i].second, ButtonVariant::Text);
        const ColorTheme value = themes[i].first;
        button->SetOnClick([this, value] { Controller().SetColorTheme(value); });
        themeButtons_[i] = button;
        colorRow->Add(button, 0, wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    }
    appearanceSizer->Add(colorRow, 0, wxEXPAND);
    pages::AddSpacer(appearanceSizer, appearance, SpaceDp(Space::Md));

    animations_ = new Switch(appearance);
    animations_->SetOnChange([this](bool value) { Controller().SetAnimationsEnabled(value); });
    AddSettingRow(appearanceSizer, appearance, "Animations", animations_);
    pages::AddSpacer(appearanceSizer, appearance, SpaceDp(Space::Sm));

    reducedMotion_ = new Switch(appearance);
    reducedMotion_->SetOnChange([this](bool value) { Controller().SetReducedMotion(value); });
    AddSettingRow(appearanceSizer, appearance, "Reduced motion", reducedMotion_);
    sizer->Add(appearance, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    // --- General -----------------------------------------------------------
    wxSizer* generalSizer = nullptr;
    auto* general = MakeSection(this, "General", &generalSizer);
    generalSizer->Add(MakeDisabledItem(general, "Startup page", "Defaults to Dashboard",
                                       "Not implemented"),
                      0, wxEXPAND);
    generalSizer->Add(MakeDisabledItem(general, "Minimize behavior",
                                       "Keep running in the background", "Not implemented"),
                      0, wxEXPAND);
    generalSizer->Add(MakeDisabledItem(general, "Close behavior",
                                       "Stop the core when the window closes", "Not implemented"),
                      0, wxEXPAND);
    sizer->Add(general, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    // --- Mihomo ------------------------------------------------------------
    wxSizer* mihomoSizer = nullptr;
    auto* mihomo = MakeSection(this, "Mihomo", &mihomoSizer);
    kernelPathItem_ = MakeDisabledItem(mihomo, "Kernel path", "Not configured", "P2.2");
    kernelPathItem_->SetLeadingIcon(IconId::Settings);
    mihomoSizer->Add(kernelPathItem_, 0, wxEXPAND);
    apiItem_ = MakeDisabledItem(mihomo, "API address", "Not configured", "P2.2");
    apiItem_->SetLeadingIcon(IconId::Wifi);
    mihomoSizer->Add(apiItem_, 0, wxEXPAND);
    secretItem_ = MakeDisabledItem(mihomo, "Secret", "Not configured", "P2.2");
    secretItem_->SetLeadingIcon(IconId::Info);
    mihomoSizer->Add(secretItem_, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Md));
    AddParagraph(mihomoSizer, mihomo,
                 "Kernel supervision and API credentials are wired in the next stage.");
    sizer->Add(mihomo, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    // --- Network -----------------------------------------------------------
    wxSizer* networkSizer = nullptr;
    auto* network = MakeSection(this, "Network", &networkSizer);
    networkSizer->Add(MakeDisabledItem(network, "System proxy",
                                       "Register the OS proxy settings", "Not implemented"),
                      0, wxEXPAND);
    networkSizer->Add(MakeDisabledItem(network, "TUN mode", "Virtual network adapter",
                                       "Not implemented"),
                      0, wxEXPAND);
    sizer->Add(network, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    // --- About -------------------------------------------------------------
    wxSizer* aboutSizer = nullptr;
    auto* about = MakeSection(this, "About", &aboutSizer);
    aboutSizer->Add(new TextBlock(about, "ClashYuka", TypeStyle::Headline), 0, wxEXPAND);
    AddParagraph(aboutSizer, about,
                 wxString::FromUTF8("Version 0.2.0 · P2.1 application shell"), 200);
    pages::AddSpacer(aboutSizer, about, SpaceDp(Space::Sm));
    AddParagraph(aboutSizer, about,
                 wxString::FromUTF8("C++20 · wxWidgets 3.3.1 · MD3 painted with "
                                    "wxGraphicsContext"));
    const core::RuntimeInfo info = core::GetRuntimeInfo();
    const wxString platformLine =
        wxString::FromUTF8(info.platformName + " · " + info.compiler + " · " +
                           (info.elevated ? "elevated" : "standard user"));
    AddParagraph(aboutSizer, about, platformLine, 170);
    pages::AddSpacer(aboutSizer, about, SpaceDp(Space::Md));

    auto* project = new ListItem(about);
    project->SetTitle("Project page");
    project->SetSubtitle("No repository URL is configured in this build");
    project->SetTrailingText(wxString::FromUTF8("—"));
    project->SetLeadingIcon(IconId::Info);
    project->SetShowChevron(false);
    project->Enable(false);
    aboutSizer->Add(project, 0, wxEXPAND);
    sizer->Add(about, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void SettingsPage::ApplyState(const app::AppStateSnapshot& state) {
    if (lightButton_ == nullptr) {
        return;
    }
    lightButton_->SetVariant(state.dark ? ButtonVariant::Text : ButtonVariant::FilledTonal);
    darkButton_->SetVariant(state.dark ? ButtonVariant::FilledTonal : ButtonVariant::Text);

    const ColorTheme order[] = {ColorTheme::Purple, ColorTheme::Blue, ColorTheme::Teal,
                                ColorTheme::Orange};
    for (std::size_t i = 0; i < themeButtons_.size(); ++i) {
        themeButtons_[i]->SetVariant(order[i] == state.theme ? ButtonVariant::FilledTonal
                                                             : ButtonVariant::Text);
    }
    animations_->SetValue(state.animationsEnabled, false);
    reducedMotion_->SetValue(state.reducedMotion, false);

    kernelPathItem_->SetSubtitle(state.kernelPath.empty()
                                     ? wxString("Not configured")
                                     : wxString::FromUTF8(state.kernelPath));
    apiItem_->SetSubtitle(state.apiAddress.empty() ? wxString("Not configured")
                                                   : wxString::FromUTF8(state.apiAddress));
    secretItem_->SetSubtitle("Not configured");
}

}  // namespace clashyuka::ui
