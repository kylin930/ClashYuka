#include "clashyuka/ui/demo_frame.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/version.h>

#include "clashyuka/core/app_info.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/components/dialog.h"
#include "clashyuka/ui/components/divider.h"
#include "clashyuka/ui/components/icon_button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/navigation_rail.h"
#include "clashyuka/ui/components/progress_indicator.h"
#include "clashyuka/ui/components/snackbar.h"
#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/switch.h"
#include "clashyuka/ui/components/tab_bar.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/drawing/motion_tokens.h"
#include "clashyuka/ui/theme/shape_tokens.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

class SurfacePanel final : public wxPanel, public ThemeObserver {
public:
    explicit SurfacePanel(wxWindow* parent) : wxPanel(parent) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetDoubleBuffered(true);
        ThemeManager::Instance().AddObserver(this);
        Bind(wxEVT_PAINT, &SurfacePanel::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    }

    ~SurfacePanel() override { ThemeManager::Instance().RemoveObserver(this); }

    void OnThemeChanged() override { Refresh(); }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(ToWx(ThemeManager::Instance().Colors().surface)));
        dc.Clear();
    }
};

void AddSpacer(wxSizer* sizer, wxWindow* window, int dp) {
    sizer->AddSpacer(window->FromDIP(dp));
}

SurfaceCard* MakeCard(wxWindow* parent, const wxString& title, const wxString& body) {
    auto* card = new SurfaceCard(parent, SurfaceLevel::Low);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new TextBlock(card, title, TypeStyle::Title), 0, wxEXPAND);
    sizer->AddSpacer(card->FromDIP(SpaceDp(Space::Sm)));
    sizer->Add(new TextBlock(card, body, TypeStyle::Body, 190), 0, wxEXPAND);
    card->SetContent(sizer, 20);
    return card;
}

}  // namespace

DemoFrame::DemoFrame()
    : wxFrame(nullptr, wxID_ANY, "ClashYuka - MD3 Design System", wxDefaultPosition,
              wxSize(1120, 760)) {
    ThemeManager::Instance().AddObserver(this);
    BuildUi();
    SelectPage(0);
    ApplyThemeToChrome();
    Centre();

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        ThemeManager::Instance().SetKeyboardNavigation(true);
        if (event.GetKeyCode() == WXK_F1) {
            SelectPage(0);
            return;
        }
        if (event.GetKeyCode() == WXK_F2) {
            SelectPage(1);
            return;
        }
        if (event.GetKeyCode() == WXK_F3) {
            SelectPage(2);
            return;
        }
        if (event.GetKeyCode() == WXK_F4) {
            SelectPage(3);
            return;
        }
        if (event.GetKeyCode() == WXK_F5) {
            SelectPage(4);
            return;
        }
        if (event.ControlDown()) {
            switch (event.GetKeyCode()) {
                case 'D':
                    ThemeManager::Instance().ToggleDarkMode();
                    return;
                case '1':
                    ThemeManager::Instance().SetColorTheme(ColorTheme::Purple);
                    return;
                case '2':
                    ThemeManager::Instance().SetColorTheme(ColorTheme::Blue);
                    return;
                case '3':
                    ThemeManager::Instance().SetColorTheme(ColorTheme::Teal);
                    return;
                case '4':
                    ThemeManager::Instance().SetColorTheme(ColorTheme::Orange);
                    return;
                case 'M':
                    ThemeManager::Instance().SetAnimationsEnabled(
                        !ThemeManager::Instance().AnimationsEnabled());
                    return;
                case 'R':
                    ThemeManager::Instance().SetReducedMotion(
                        !ThemeManager::Instance().ReducedMotion());
                    return;
                case 'T':
                    if (tabBar_ != nullptr && tabBook_ != nullptr) {
                        const int next = (tabBar_->GetSelected() + 1) % 3;
                        tabBar_->SetSelected(next);
                        tabBook_->SetSelection(next);
                        SelectPage(1);
                    }
                    return;
                case 'S':
                    Snackbar::Present(this, "Configuration saved", "Undo", [] {}, 12000);
                    return;
                case 'E': {
                    Dialog dialog(this, "Apply changes?",
                                  "This is a modal MD3 dialog with custom painted action buttons.");
                    dialog.AddAction("Cancel", ButtonVariant::Text, wxID_CANCEL);
                    dialog.AddAction("Apply", ButtonVariant::Filled, wxID_OK);
                    dialog.ShowModal();
                    return;
                }
                default:
                    break;
            }
        }
        event.Skip();
    });
}

DemoFrame::~DemoFrame() {
    AnimationManager::Instance().StopAll();
    ThemeManager::Instance().RemoveObserver(this);
}

void DemoFrame::BuildUi() {
    root_ = new wxPanel(this);
    auto* rootSizer = new wxBoxSizer(wxHORIZONTAL);

    rail_ = new NavigationRail(root_, true);
    rail_->SetItems({
        {IconId::Dashboard, "Overview", 0},
        {IconId::Palette, "Components", 0},
        {IconId::Activity, "Feedback", 3},
        {IconId::Refresh, "Motion", 0},
        {IconId::Info, "About", 0},
    });
    rail_->SetSelected(0);
    rail_->SetOnSelect([this](int index) { SelectPage(index); });
    rootSizer->Add(rail_, 0, wxEXPAND);

    auto* rightColumn = new wxBoxSizer(wxVERTICAL);

    header_ = new wxPanel(root_);
    auto* headerSizer = new wxBoxSizer(wxVERTICAL);
    auto* titleRow = new wxBoxSizer(wxVERTICAL);
    pageTitle_ = new TextBlock(header_, "Overview", TypeStyle::Headline);
    pageSubtitle_ = new TextBlock(header_, "Native MD3 design system for ClashYuka",
                                  TypeStyle::Body, 170);
    titleRow->Add(pageTitle_, 0, wxEXPAND);
    titleRow->Add(pageSubtitle_, 0, wxEXPAND);
    headerSizer->Add(titleRow, 0, wxEXPAND);

    AddSpacer(headerSizer, header_, SpaceDp(Space::Lg));

    auto* themeRow = new wxBoxSizer(wxHORIZONTAL);
    darkButton_ = new Button(header_, "Dark mode", ButtonVariant::FilledTonal);
    darkButton_->SetOnClick([] { ThemeManager::Instance().ToggleDarkMode(); });
    themeRow->Add(darkButton_, 0, wxRIGHT, header_->FromDIP(SpaceDp(Space::Sm)));

    const std::pair<ColorTheme, const char*> themes[] = {
        {ColorTheme::Purple, "Purple"},
        {ColorTheme::Blue, "Blue"},
        {ColorTheme::Teal, "Teal"},
        {ColorTheme::Orange, "Orange"},
    };
    for (const auto& theme : themes) {
        auto* button = new Button(header_, theme.second, ButtonVariant::Text);
        const ColorTheme value = theme.first;
        button->SetOnClick([value] { ThemeManager::Instance().SetColorTheme(value); });
        themeButtons_.push_back(button);
        themeRow->Add(button, 0, wxRIGHT, header_->FromDIP(SpaceDp(Space::Xs)));
    }

    themeRow->AddStretchSpacer();
    auto* motionLabel = new TextBlock(header_, "Animations", TypeStyle::Label, 190);
    motionSwitch_ = new Switch(header_);
    motionSwitch_->SetOnChange([](bool value) {
        ThemeManager::Instance().SetAnimationsEnabled(value);
    });
    themeRow->Add(motionLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                  header_->FromDIP(SpaceDp(Space::Sm)));
    themeRow->Add(motionSwitch_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                  header_->FromDIP(SpaceDp(Space::Lg)));

    auto* reducedLabel = new TextBlock(header_, "Reduced motion", TypeStyle::Label, 190);
    reducedSwitch_ = new Switch(header_);
    reducedSwitch_->SetOnChange([](bool value) {
        ThemeManager::Instance().SetReducedMotion(value);
    });
    themeRow->Add(reducedLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                  header_->FromDIP(SpaceDp(Space::Sm)));
    themeRow->Add(reducedSwitch_, 0, wxALIGN_CENTER_VERTICAL);

    headerSizer->Add(themeRow, 0, wxEXPAND);
    header_->SetSizer(headerSizer);
    rightColumn->Add(header_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                     root_->FromDIP(SpaceDp(Space::Xxl)));

    content_ = new wxScrolledWindow(root_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxVSCROLL);
    content_->SetScrollRate(0, root_->FromDIP(12));
    contentSizer_ = new wxBoxSizer(wxVERTICAL);
    content_->SetSizer(contentSizer_);
    rightColumn->Add(content_, 1, wxEXPAND | wxALL, root_->FromDIP(SpaceDp(Space::Xxl)));

    pages_.push_back(BuildOverviewPage(content_));
    pages_.push_back(BuildComponentsPage(content_));
    pages_.push_back(BuildFeedbackPage(content_));
    pages_.push_back(BuildMotionPage(content_));
    pages_.push_back(BuildAboutPage(content_));
    for (auto* page : pages_) {
        contentSizer_->Add(page, 1, wxEXPAND);
    }

    rootSizer->Add(rightColumn, 1, wxEXPAND);
    root_->SetSizer(rootSizer);
}

wxPanel* DemoFrame::BuildOverviewPage(wxWindow* parent) {
    auto* page = new SurfacePanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(MakeCard(page, "Material Design 3, drawn natively",
                        "Native C++ wxWidgets controls painted with wxGraphicsContext."),
               0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Lg));

    sizer->Add(MakeCard(page, "Theme aware",
                        "Switch theme or dark mode above; every control repaints instantly."),
               0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Lg));

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    auto* primary = new Button(page, "Primary action", ButtonVariant::Filled,
                               IconId::Check);
    auto* tonal = new Button(page, "Secondary", ButtonVariant::FilledTonal);
    auto* outlined = new Button(page, "Outlined", ButtonVariant::Outlined);
    auto* text = new Button(page, "Text button", ButtonVariant::Text);
    actions->Add(primary, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    actions->Add(tonal, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    actions->Add(outlined, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    actions->Add(text, 0);
    sizer->Add(actions, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Xl));
    auto* list = new ListItem(page);
    list->SetTitle("Native rendering");
    list->SetSubtitle("2D vector drawing through wxGraphicsContext");
    list->SetLeadingIcon(IconId::Palette);
    list->SetTrailingText("MD3");
    list->SetShowDivider(true);
    sizer->Add(list, 0, wxEXPAND);

    auto* list2 = new ListItem(page);
    list2->SetTitle("Mihomo controller ready");
    list2->SetSubtitle("Process, REST and WebSocket layers are already tested");
    list2->SetLeadingIcon(IconId::Wifi);
    list2->SetShowChevron(true);
    sizer->Add(list2, 0, wxEXPAND);

    page->SetSizer(sizer);
    return page;
}

wxPanel* DemoFrame::BuildComponentsPage(wxWindow* parent) {
    auto* page = new SurfacePanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    tabBar_ = new TabBar(page);
    tabBar_->SetTabs({"Buttons", "Selection", "Surfaces"});
    sizer->Add(tabBar_, 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Lg));

    tabBook_ = new wxSimplebook(page);
    tabBar_->SetOnSelect([this](int index) {
        tabBook_->SetSelection(index);
        tabBook_->Layout();
        auto* tabPage = tabBook_->GetPage(index);
        if (tabPage == nullptr || !ThemeManager::Instance().MotionEnabled()) {
            return;
        }
        const wxPoint base = tabPage->GetPosition();
        tabPage->SetPosition(base + wxPoint(0, FromDIP(6)));
        tabPageAnimation_ = AnimationManager::Instance().Animate(
            0.0, 1.0, motion::PageMs, motion::Enter, [this, tabPage, base](double value) {
                tabPage->SetPosition(
                    wxPoint(base.x, base.y + static_cast<int>(FromDIP(6) * (1.0 - value))));
            });
    });
    sizer->Add(tabBook_, 1, wxEXPAND);

    // Buttons tab
    auto* buttonsPage = new SurfacePanel(tabBook_);
    auto* buttonsSizer = new wxBoxSizer(wxVERTICAL);
    buttonsSizer->Add(new TextBlock(buttonsPage, "Button variants", TypeStyle::Title), 0,
                      wxEXPAND);
    AddSpacer(buttonsSizer, buttonsPage, SpaceDp(Space::Sm));
    auto* variants = new wxBoxSizer(wxHORIZONTAL);
    variants->Add(new Button(buttonsPage, "Filled", ButtonVariant::Filled), 0,
                  wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    variants->Add(new Button(buttonsPage, "Tonal", ButtonVariant::FilledTonal), 0,
                  wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    variants->Add(new Button(buttonsPage, "Outlined", ButtonVariant::Outlined), 0,
                  wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    variants->Add(new Button(buttonsPage, "Text", ButtonVariant::Text), 0);
    buttonsSizer->Add(variants, 0, wxEXPAND);

    AddSpacer(buttonsSizer, buttonsPage, SpaceDp(Space::Lg));
    buttonsSizer->Add(new TextBlock(buttonsPage, "States", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(buttonsSizer, buttonsPage, SpaceDp(Space::Sm));
    auto* states = new wxBoxSizer(wxHORIZONTAL);
    auto* disabledFilled = new Button(buttonsPage, "Disabled", ButtonVariant::Filled);
    disabledFilled->Enable(false);
    auto* disabledOutlined = new Button(buttonsPage, "Disabled", ButtonVariant::Outlined);
    disabledOutlined->Enable(false);
    auto* iconButton = new Button(buttonsPage, "With icon", ButtonVariant::FilledTonal,
                                  IconId::Add);
    states->Add(disabledFilled, 0, wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    states->Add(disabledOutlined, 0, wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    states->Add(iconButton, 0);
    buttonsSizer->Add(states, 0, wxEXPAND);

    AddSpacer(buttonsSizer, buttonsPage, SpaceDp(Space::Lg));
    buttonsSizer->Add(new TextBlock(buttonsPage, "Icon buttons", TypeStyle::Title), 0,
                      wxEXPAND);
    AddSpacer(buttonsSizer, buttonsPage, SpaceDp(Space::Sm));
    auto* icons = new wxBoxSizer(wxHORIZONTAL);
    icons->Add(new IconButton(buttonsPage, IconId::Settings, IconButtonVariant::Standard), 0,
               wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(buttonsPage, IconId::Check, IconButtonVariant::Filled), 0,
               wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(buttonsPage, IconId::Palette, IconButtonVariant::FilledTonal),
               0, wxRIGHT, buttonsPage->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(buttonsPage, IconId::Info, IconButtonVariant::Outlined), 0);
    buttonsSizer->Add(icons, 0, wxEXPAND);
    buttonsPage->SetSizer(buttonsSizer);

    // Selection tab
    auto* selectionPage = new SurfacePanel(tabBook_);
    auto* selectionSizer = new wxBoxSizer(wxVERTICAL);
    selectionSizer->Add(new TextBlock(selectionPage, "Switches", TypeStyle::Title), 0,
                        wxEXPAND);
    AddSpacer(selectionSizer, selectionPage, SpaceDp(Space::Sm));

    auto addSwitchRow = [&](const char* label, bool initial) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new TextBlock(selectionPage, label, TypeStyle::Body), 1,
                 wxALIGN_CENTER_VERTICAL);
        auto* control = new Switch(selectionPage);
        control->SetValue(initial, false);
        row->Add(control, 0, wxALIGN_CENTER_VERTICAL);
        selectionSizer->Add(row, 0, wxEXPAND | wxTOP, selectionPage->FromDIP(SpaceDp(Space::Sm)));
    };
    addSwitchRow("Enable system proxy", true);
    addSwitchRow("Start with Windows", false);
    addSwitchRow("Allow LAN connections", false);

    AddSpacer(selectionSizer, selectionPage, SpaceDp(Space::Xl));
    selectionSizer->Add(new TextBlock(selectionPage, "List items", TypeStyle::Title), 0,
                        wxEXPAND);
    AddSpacer(selectionSizer, selectionPage, SpaceDp(Space::Sm));

    auto* item1 = new ListItem(selectionPage);
    item1->SetTitle("Rule mode");
    item1->SetSubtitle("Traffic is routed by the rule list");
    item1->SetLeadingIcon(IconId::Dashboard);
    item1->SetTrailingText("Active");
    item1->SetShowDivider(true);
    selectionSizer->Add(item1, 0, wxEXPAND);

    auto* item2 = new ListItem(selectionPage);
    item2->SetTitle("Log level");
    item2->SetSubtitle("Informational messages");
    item2->SetLeadingIcon(IconId::Activity);
    item2->SetTrailingText("Info");
    item2->SetShowDivider(true);
    selectionSizer->Add(item2, 0, wxEXPAND);

    auto* item3 = new ListItem(selectionPage);
    item3->SetTitle("External controller");
    item3->SetSubtitle("127.0.0.1:9090");
    item3->SetLeadingIcon(IconId::Wifi);
    item3->SetShowChevron(true);
    selectionSizer->Add(item3, 0, wxEXPAND);
    selectionPage->SetSizer(selectionSizer);

    // Surfaces tab
    auto* surfacesPage = new SurfacePanel(tabBook_);
    auto* surfacesSizer = new wxBoxSizer(wxVERTICAL);
    surfacesSizer->Add(new TextBlock(surfacesPage, "Surface levels", TypeStyle::Title), 0,
                       wxEXPAND);
    AddSpacer(surfacesSizer, surfacesPage, SpaceDp(Space::Sm));

    const SurfaceLevel levels[] = {SurfaceLevel::Lowest, SurfaceLevel::Low,
                                   SurfaceLevel::Container, SurfaceLevel::High,
                                   SurfaceLevel::Highest};
    const char* names[] = {"Lowest", "Low", "Container", "High", "Highest"};
    for (int i = 0; i < 5; ++i) {
        auto* card = new SurfaceCard(surfacesPage, levels[i]);
        card->SetOutlined(i == 2);
        auto* cardSizer = new wxBoxSizer(wxVERTICAL);
        cardSizer->Add(new TextBlock(card, names[i], TypeStyle::Title), 0, wxEXPAND);
        cardSizer->Add(new TextBlock(card, "Rounded 16dp surface with theme tokens",
                                     TypeStyle::Body, 170),
                       0, wxEXPAND);
        card->SetContent(cardSizer, 16);
        surfacesSizer->Add(card, 0, wxEXPAND | wxTOP, surfacesPage->FromDIP(SpaceDp(Space::Sm)));
    }
    surfacesSizer->Add(new Divider(surfacesPage), 0, wxEXPAND | wxTOP,
                       surfacesPage->FromDIP(SpaceDp(Space::Lg)));
    surfacesPage->SetSizer(surfacesSizer);

    tabBook_->AddPage(buttonsPage, "Buttons");
    tabBook_->AddPage(selectionPage, "Selection");
    tabBook_->AddPage(surfacesPage, "Surfaces");

    page->SetSizer(sizer);
    return page;
}

wxPanel* DemoFrame::BuildFeedbackPage(wxWindow* parent) {
    auto* page = new SurfacePanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(page, "Dialogs and snackbars", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    auto* dialogButton = new Button(page, "Show dialog", ButtonVariant::Filled);
    dialogButton->SetOnClick([this] {
        Dialog dialog(this, "Apply changes?",
                      "This is a modal MD3 dialog with custom painted action buttons.");
        dialog.AddAction("Cancel", ButtonVariant::Text, wxID_CANCEL);
        dialog.AddAction("Apply", ButtonVariant::Filled, wxID_OK);
        dialog.ShowModal();
    });
    auto* snackbarButton = new Button(page, "Show snackbar", ButtonVariant::FilledTonal);
    snackbarButton->SetOnClick([this] {
        Snackbar::Present(this, "Configuration saved", "Undo", [] {}, 12000);
    });
    actions->Add(dialogButton, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    actions->Add(snackbarButton, 0);
    sizer->Add(actions, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Xl));
    sizer->Add(new TextBlock(page, "Linear progress", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));

    auto* linearDeterminate = new ProgressIndicator(page, ProgressKind::Linear);
    linearDeterminate->SetMinSize(wxSize(page->FromDIP(320), page->FromDIP(4)));
    linearDeterminate->SetValue(0.62);
    sizer->Add(linearDeterminate, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Md));
    auto* linearIndeterminate = new ProgressIndicator(page, ProgressKind::Linear);
    linearIndeterminate->SetMinSize(wxSize(page->FromDIP(320), page->FromDIP(4)));
    linearIndeterminate->SetIndeterminate(true);
    sizer->Add(linearIndeterminate, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Xl));
    sizer->Add(new TextBlock(page, "Circular progress", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));
    auto* circularRow = new wxBoxSizer(wxHORIZONTAL);

    auto* circularDeterminate = new ProgressIndicator(page, ProgressKind::Circular);
    circularDeterminate->SetValue(0.35);
    auto* circularIndeterminate = new ProgressIndicator(page, ProgressKind::Circular);
    circularIndeterminate->SetIndeterminate(true);
    circularRow->Add(circularDeterminate, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Lg)));
    circularRow->Add(circularIndeterminate, 0);
    sizer->Add(circularRow, 0, wxEXPAND);

    page->SetSizer(sizer);
    return page;
}

wxPanel* DemoFrame::BuildMotionPage(wxWindow* parent) {
    auto* page = new SurfacePanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(page, "Press feedback", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* rippleButton = new Button(page, "Ripple", ButtonVariant::Filled, IconId::Add);
    auto* tonalButton = new Button(page, "Tonal", ButtonVariant::FilledTonal);
    auto* outlinedButton = new Button(page, "Outlined", ButtonVariant::Outlined);
    auto* textButton = new Button(page, "Text", ButtonVariant::Text);
    buttons->Add(rippleButton, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    buttons->Add(tonalButton, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    buttons->Add(outlinedButton, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    buttons->Add(textButton, 0);
    sizer->Add(buttons, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Lg));
    sizer->Add(new TextBlock(page, "Icon buttons", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));
    auto* icons = new wxBoxSizer(wxHORIZONTAL);
    icons->Add(new IconButton(page, IconId::Sun, IconButtonVariant::Standard), 0, wxRIGHT,
               page->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(page, IconId::Check, IconButtonVariant::Filled), 0, wxRIGHT,
               page->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(page, IconId::Palette, IconButtonVariant::FilledTonal), 0,
               wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    icons->Add(new IconButton(page, IconId::Info, IconButtonVariant::Outlined), 0);
    sizer->Add(icons, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Lg));
    auto* switchRow = new wxBoxSizer(wxHORIZONTAL);
    switchRow->Add(new TextBlock(page, "Switch animation", TypeStyle::Body), 1,
                   wxALIGN_CENTER_VERTICAL);
    auto* demoSwitch = new Switch(page);
    demoSwitch->SetValue(true, false);
    switchRow->Add(demoSwitch, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(switchRow, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Xl));
    sizer->Add(new TextBlock(page, "Smooth progress", TypeStyle::Title), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Sm));
    motionProgress_ = new ProgressIndicator(page, ProgressKind::Linear);
    motionProgress_->SetMinSize(wxSize(page->FromDIP(320), page->FromDIP(4)));
    motionProgress_->SetValue(motionValue_);
    sizer->Add(motionProgress_, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Md));
    auto* progressActions = new wxBoxSizer(wxHORIZONTAL);
    auto* advance = new Button(page, "Advance progress", ButtonVariant::Outlined);
    advance->SetOnClick([this] {
        motionValue_ = motionValue_ >= 0.99 ? 0.15 : motionValue_ + 0.17;
        if (motionProgress_ != nullptr) {
            motionProgress_->SetValue(motionValue_);
        }
    });
    progressActions->Add(advance, 0, wxRIGHT, page->FromDIP(SpaceDp(Space::Sm)));
    auto* snackbarButton = new Button(page, "Show snackbar", ButtonVariant::Text);
    snackbarButton->SetOnClick([this] {
        Snackbar::Present(this, "Motion is enabled", "Undo", [] {}, 8000);
    });
    progressActions->Add(snackbarButton, 0);
    sizer->Add(progressActions, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Md));
    auto* indeterminateRow = new wxBoxSizer(wxHORIZONTAL);
    auto* linearIndeterminate = new ProgressIndicator(page, ProgressKind::Linear);
    linearIndeterminate->SetMinSize(wxSize(page->FromDIP(220), page->FromDIP(4)));
    linearIndeterminate->SetIndeterminate(true);
    auto* circularIndeterminate = new ProgressIndicator(page, ProgressKind::Circular);
    circularIndeterminate->SetIndeterminate(true);
    indeterminateRow->Add(linearIndeterminate, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                          page->FromDIP(SpaceDp(Space::Lg)));
    indeterminateRow->Add(circularIndeterminate, 0);
    sizer->Add(indeterminateRow, 0, wxEXPAND);

    AddSpacer(sizer, page, SpaceDp(Space::Xl));
    sizer->Add(new TextBlock(page, "Tip: F1-F5 switch pages, Ctrl+M toggles animation, "
                                   "Ctrl+R enables reduced motion.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);

    page->SetSizer(sizer);
    return page;
}

wxPanel* DemoFrame::BuildAboutPage(wxWindow* parent) {
    auto* page = new SurfacePanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    const core::RuntimeInfo info = core::GetRuntimeInfo();
    const std::string runtimeLine =
        std::string(wxString(wxVERSION_STRING).ToStdString()) + ", " + info.cppStandard + ", " +
        info.compiler;
    sizer->Add(MakeCard(page, "Built with", wxString::FromUTF8(runtimeLine)), 0, wxEXPAND);
    AddSpacer(sizer, page, SpaceDp(Space::Lg));

    auto* platform = new ListItem(page);
    platform->SetTitle("Platform");
    platform->SetSubtitle(wxString::FromUTF8(
        info.platformName +
        (info.elevated ? std::string(" (elevated)") : std::string(" (standard user)"))));
    platform->SetLeadingIcon(IconId::Info);
    platform->SetShowDivider(true);
    sizer->Add(platform, 0, wxEXPAND);

    auto* runtime = new ListItem(page);
    runtime->SetTitle("Rendering");
    runtime->SetSubtitle("wxGraphicsContext with buffered painting");
    runtime->SetLeadingIcon(IconId::Palette);
    runtime->SetShowDivider(true);
    sizer->Add(runtime, 0, wxEXPAND);

    auto* shortcuts = new ListItem(page);
    shortcuts->SetTitle("Shortcuts");
    shortcuts->SetSubtitle("F1-F5 pages, Ctrl+D dark, Ctrl+1..4 color, Ctrl+M motion, Ctrl+R reduced");
    shortcuts->SetLeadingIcon(IconId::Settings);
    shortcuts->SetShowChevron(false);
    sizer->Add(shortcuts, 0, wxEXPAND);

    page->SetSizer(sizer);
    return page;
}

void DemoFrame::SelectPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) {
        return;
    }
    pageAnimation_.reset();
    contentSizer_->Layout();

    currentPage_ = index;
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
        pages_[i]->Show(i == index);
    }
    contentSizer_->Layout();
    content_->FitInside();
    content_->Scroll(0, 0);
    rail_->SetSelected(index);

    if (index == 0) {
        pageTitle_->SetText("Overview");
        pageSubtitle_->SetText("Native MD3 design system for ClashYuka");
    } else if (index == 1) {
        pageTitle_->SetText("Components");
        pageSubtitle_->SetText("Buttons, selection controls, surfaces and tabs");
    } else if (index == 2) {
        pageTitle_->SetText("Feedback");
        pageSubtitle_->SetText("Dialogs, snackbars and progress indicators");
    } else if (index == 3) {
        pageTitle_->SetText("Motion");
        pageSubtitle_->SetText("Ripple, press, selection and page transitions");
    } else {
        pageTitle_->SetText("About");
        pageSubtitle_->SetText("Runtime details and keyboard shortcuts");
    }
    header_->Layout();

    if (ThemeManager::Instance().MotionEnabled()) {
        wxPanel* page = pages_[index];
        const wxPoint base = page->GetPosition();
        page->SetPosition(base + wxPoint(0, FromDIP(8)));
        pageAnimation_ = AnimationManager::Instance().Animate(
            0.0, 1.0, motion::PageMs, motion::Enter, [this, page, base](double value) {
                page->SetPosition(
                    wxPoint(base.x, base.y + static_cast<int>(FromDIP(8) * (1.0 - value))));
            });
    }
}

void DemoFrame::UpdateThemeControls() {
    ThemeManager& manager = ThemeManager::Instance();
    if (darkButton_ != nullptr) {
        darkButton_->SetLabel(manager.IsDark() ? "Light mode" : "Dark mode");
    }
    const ColorTheme active = manager.GetColorTheme();
    const ColorTheme order[] = {ColorTheme::Purple, ColorTheme::Blue, ColorTheme::Teal,
                                ColorTheme::Orange};
    for (std::size_t i = 0; i < themeButtons_.size() && i < 4; ++i) {
        themeButtons_[i]->SetVariant(order[i] == active ? ButtonVariant::FilledTonal
                                                       : ButtonVariant::Text);
    }
    if (motionSwitch_ != nullptr) {
        motionSwitch_->SetValue(manager.AnimationsEnabled(), false);
    }
    if (reducedSwitch_ != nullptr) {
        reducedSwitch_->SetValue(manager.ReducedMotion(), false);
    }
}

void DemoFrame::ApplyThemeToChrome() {
    const ColorScheme& colors = ThemeManager::Instance().Colors();
    SetBackgroundColour(ToWx(colors.surface));
    root_->SetBackgroundColour(ToWx(colors.surface));
    header_->SetBackgroundColour(ToWx(colors.surface));
    content_->SetBackgroundColour(ToWx(colors.surface));
    Refresh();
    UpdateThemeControls();
}

void DemoFrame::OnThemeChanged() {
    ApplyThemeToChrome();
    Layout();
}

}  // namespace clashyuka::ui
