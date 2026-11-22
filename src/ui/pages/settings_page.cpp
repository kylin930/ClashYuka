#include "clashyuka/ui/pages/settings_page.h"

#include <utility>

#include <wx/filedlg.h>
#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/app/app_text.h"
#include "clashyuka/core/app_info.h"
#include "clashyuka/runtime/settings_store.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/search_field.h"
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
    kernelStatusLine_ = new TextBlock(mihomo, "Kernel: Not started", TypeStyle::Label, 170);
    mihomoSizer->Add(kernelStatusLine_, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Sm));

    mihomoSizer->Add(new TextBlock(mihomo, "Mihomo executable", TypeStyle::Body), 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Xs));
    auto* pathRow = new wxBoxSizer(wxHORIZONTAL);
    kernelPathField_ = new SearchField(mihomo, "Path to the mihomo executable", std::nullopt);
    auto* browse = new Button(mihomo, "Browse", ButtonVariant::Outlined);
    browse->SetOnClick([this] { BrowseKernelPath(); });
    pathRow->Add(kernelPathField_, 1, wxEXPAND | wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    pathRow->Add(browse, 0, wxALIGN_CENTER_VERTICAL);
    mihomoSizer->Add(pathRow, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Md));

    auto* portRow = new wxBoxSizer(wxHORIZONTAL);
    portRow->Add(new TextBlock(mihomo, "API port", TypeStyle::Body), 0,
                 wxALIGN_CENTER_VERTICAL);
    portRow->AddStretchSpacer();
    apiPortField_ = new SearchField(mihomo, "9090", std::nullopt);
    apiPortField_->SetMinSize(wxSize(FromDIP(120), -1));
    portRow->Add(apiPortField_, 0, wxALIGN_CENTER_VERTICAL);
    mihomoSizer->Add(portRow, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Md));

    auto* secretRow = new wxBoxSizer(wxHORIZONTAL);
    secretRow->Add(new TextBlock(mihomo, "API secret", TypeStyle::Body), 0,
                   wxALIGN_CENTER_VERTICAL);
    secretRow->AddStretchSpacer();
    secretField_ = new SearchField(mihomo, "Secret", std::nullopt, true);
    secretField_->SetMinSize(wxSize(FromDIP(260), -1));
    secretRow->Add(secretField_, 0, wxALIGN_CENTER_VERTICAL);
    mihomoSizer->Add(secretRow, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Md));

    autoStart_ = new Switch(mihomo);
    AddSettingRow(mihomoSizer, mihomo, "Start kernel with the app", autoStart_);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Md));

    auto* saveRow = new wxBoxSizer(wxHORIZONTAL);
    auto* save = new Button(mihomo, "Save Mihomo settings", ButtonVariant::Filled);
    save->SetOnClick([this] { SaveMihomoSettings(); });
    saveRow->Add(save, 0);
    mihomoSizer->Add(saveRow, 0, wxEXPAND);
    pages::AddSpacer(mihomoSizer, mihomo, SpaceDp(Space::Sm));
    AddParagraph(mihomoSizer, mihomo,
                 "The secret is stored as plain text in the per-user application data "
                 "directory, with the same trust boundary as the Mihomo config. Settings "
                 "that affect a running kernel take effect after a restart.");
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
                 wxString::FromUTF8("Version 0.2.0 · P2.2 runtime integration"), 200);
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

    if (kernelStatusLine_ != nullptr) {
        kernelStatusLine_->SetText("Kernel: " + app::KernelStateText(state.kernel) + "   ·   " +
                                   "API: " + app::ApiStateText(state.api));
    }
    if (!mihomoFieldsInitialized_ && Controller().RuntimeInitialized()) {
        SyncMihomoSettings();
        mihomoFieldsInitialized_ = true;
    }
}

void SettingsPage::SyncMihomoSettings() {
    const runtime::AppSettings& settings = Controller().Settings();
    kernelPathField_->SetValue(wxString::FromUTF8(settings.mihomoPath));
    apiPortField_->SetValue(wxString::Format("%d", settings.controllerPort));
    secretField_->SetValue(wxString::FromUTF8(settings.secret));
    autoStart_->SetValue(settings.autoStartKernel, false);
}

void SettingsPage::BrowseKernelPath() {
#ifdef __WXMSW__
    const wxString wildcard = "Mihomo executable (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const wxString wildcard = "All files (*.*)|*.*";
#endif
    wxFileDialog dialog(this, "Select the Mihomo executable", wxEmptyString,
                        kernelPathField_->GetValue(), wildcard,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        kernelPathField_->SetValue(dialog.GetPath());
    }
}

void SettingsPage::SaveMihomoSettings() {
    wxString portText = apiPortField_->GetValue();
    portText.Trim(true).Trim(false);
    long port = 0;
    if (!portText.ToLong(&port)) {
        Controller().ShowMessage("The API port must be a number.");
        return;
    }

    runtime::AppSettings settings = Controller().Settings();
    settings.mihomoPath = kernelPathField_->GetValue().utf8_string();
    settings.controllerPort = static_cast<int>(port);
    settings.secret = secretField_->GetValue().utf8_string();
    settings.autoStartKernel = autoStart_->GetValue();

    wxString error;
    bool restartRequired = false;
    if (!Controller().SaveKernelSettings(settings, &error, &restartRequired)) {
        Controller().ShowMessage("Could not save Mihomo settings: " + error);
        return;
    }
    Controller().ShowMessage(restartRequired
                                 ? "Settings saved. Restart the running kernel to apply them."
                                 : "Mihomo settings saved.");
}

}  // namespace clashyuka::ui
