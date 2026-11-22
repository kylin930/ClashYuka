#include "clashyuka/ui/main_window.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include "clashyuka/app/app_text.h"
#include "clashyuka/app/page_registry.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/dialog.h"
#include "clashyuka/ui/components/icon_button.h"
#include "clashyuka/ui/components/navigation_rail.h"
#include "clashyuka/ui/components/snackbar.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/drawing/motion_tokens.h"
#include "clashyuka/ui/drawing/painter.h"
#include "clashyuka/ui/pages/page_base.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {

// Bottom status strip: kernel/API state on the left, version and proxy mode on
// the right. All values come from AppState; nothing is simulated.
class StatusBarPanel final : public wxPanel, public ThemeObserver {
public:
    explicit StatusBarPanel(wxWindow* parent) : wxPanel(parent) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetDoubleBuffered(true);
        SetMinSize(wxSize(-1, FromDIP(44)));
        ThemeManager::Instance().AddObserver(this);
        Bind(wxEVT_PAINT, &StatusBarPanel::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    }

    ~StatusBarPanel() override { ThemeManager::Instance().RemoveObserver(this); }

    void SetStatus(app::KernelState kernel, app::ApiState api, const std::string& version,
                   app::ProxyMode mode) {
        kernel_ = kernel;
        api_ = api;
        version_ = app::FormatVersion(version);
        mode_ = app::ProxyModeText(mode);
        Refresh();
    }

    void OnThemeChanged() override { Refresh(); }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const ColorScheme& colors = ThemeManager::Instance().Colors();
        dc.SetBackground(wxBrush(ToWx(colors.surfaceContainerLow)));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc = CreateGraphicsContext(dc);
        if (!gc) {
            return;
        }
        Painter painter(gc.get());
        const wxSize size = GetClientSize();
        painter.DrawLine(0, 0, size.x, 0, colors.outlineVariant, 1.0);

        const wxFont font =
            MakeFont(ThemeManager::Instance().Type().Get(TypeStyle::Label), this);
        const int padding = FromDIP(SpaceDp(Space::Xxl));
        const int radius = FromDIP(4);
        const int centerY = size.y / 2;

        Color dot = colors.outline;
        switch (kernel_) {
            case app::KernelState::Running:
                dot = colors.primary;
                break;
            case app::KernelState::Starting:
            case app::KernelState::Stopping:
                dot = colors.tertiary;
                break;
            case app::KernelState::Error:
                dot = colors.error;
                break;
            case app::KernelState::NotStarted:
                break;
        }
        painter.FillCircle(padding + radius, centerY, radius, dot);

        const wxString separator = wxString::FromUTF8("   ·   ");
        const wxString left =
            "Mihomo: " + app::KernelStateText(kernel_) + separator + app::ApiStateText(api_);
        painter.DrawText(left.utf8_string(),
                         wxRect(padding + radius * 2 + FromDIP(10), 0, size.x / 2, size.y),
                         font, WithAlpha(colors.onSurface, 190));

        const wxString right = "Version: " + version_ + separator + mode_;
        const int rightWidth = painter.MeasureText(right.utf8_string(), font).x;
        painter.DrawText(right.utf8_string(),
                         wxRect(size.x - padding - rightWidth, 0, rightWidth, size.y), font,
                         WithAlpha(colors.onSurface, 150),
                         wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
    }

    app::KernelState kernel_ = app::KernelState::NotStarted;
    app::ApiState api_ = app::ApiState::Disconnected;
    wxString version_ = "Unknown";
    wxString mode_ = "Rule";
};

MainWindow::MainWindow()
    : wxFrame(nullptr, wxID_ANY, "ClashYuka", wxDefaultPosition, wxDefaultSize),
      controller_(this) {
    ThemeManager::Instance().AddObserver(this);
    controller_.State().AddObserver(this);

    BuildUi();
    Bind(wxEVT_SIZE, &MainWindow::OnSize, this);
    SetMinClientSize(FromDIP(wxSize(880, 600)));
    SetClientSize(FromDIP(wxSize(1100, 720)));
    Centre();

    ActivatePage(controller_.Snapshot().currentPage);
    UpdateResponsiveRail();
}

MainWindow::~MainWindow() {
    AnimationManager::Instance().StopAll();
    controller_.State().RemoveObserver(this);
    ThemeManager::Instance().RemoveObserver(this);
}

void MainWindow::BuildUi() {
    root_ = new wxPanel(this);
    root_->SetBackgroundColour(ToWx(ThemeManager::Instance().Colors().surface));
    auto* rootSizer = new wxBoxSizer(wxHORIZONTAL);

    rail_ = new NavigationRail(root_, true);
    rail_->SetName("rail");
    root_->SetName("root");
    std::vector<NavigationRailItem> railItems;
    for (const app::PageDescriptor& page : app::PageRegistry::All()) {
        railItems.push_back({page.icon, page.title.utf8_string(), 0});
    }
    rail_->SetItems(std::move(railItems));
    rail_->SetSelected(app::PageRegistry::IndexOf(currentPage_));
    rail_->SetOnSelect([this](int index) { OnNavigate(index); });
    rootSizer->Add(rail_, 0, wxEXPAND);

    auto* rightColumn = new wxBoxSizer(wxVERTICAL);

    header_ = new wxPanel(root_);
    header_->SetName("header");
    header_->SetBackgroundColour(ToWx(ThemeManager::Instance().Colors().surface));
    auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* titleColumn = new wxBoxSizer(wxVERTICAL);
    const app::PageDescriptor& initial = app::PageRegistry::Get(currentPage_);
    pageTitle_ = new TextBlock(header_, initial.title, TypeStyle::Headline);
    pageSubtitle_ = new TextBlock(header_, initial.description, TypeStyle::Body, 170);
    titleColumn->Add(pageTitle_, 0, wxEXPAND);
    titleColumn->Add(pageSubtitle_, 0, wxEXPAND);
    headerSizer->Add(titleColumn, 1, wxALIGN_CENTER_VERTICAL);
    auto* settings = new IconButton(header_, IconId::Settings, IconButtonVariant::Standard);
    settings->SetOnClick([this] { controller_.Navigate(app::PageId::Settings); });
    headerSizer->Add(settings, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(SpaceDp(Space::Sm)));
    header_->SetSizer(headerSizer);
    rightColumn->Add(header_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM,
                     FromDIP(SpaceDp(Space::Xxl)));

    content_ = new wxScrolledWindow(root_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxVSCROLL);
    content_->SetScrollRate(0, FromDIP(12));
    contentSizer_ = new wxBoxSizer(wxVERTICAL);
    content_->SetSizer(contentSizer_);
    rightColumn->Add(content_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(SpaceDp(Space::Xxl)));

    status_ = new StatusBarPanel(root_);
    rightColumn->Add(status_, 0, wxEXPAND | wxTOP, FromDIP(SpaceDp(Space::Lg)));

    rootSizer->Add(rightColumn, 1, wxEXPAND);
    root_->SetSizer(rootSizer);
}

void MainWindow::EnsurePage(app::PageId id) {
    if (pages_.find(id) != pages_.end()) {
        return;
    }
    const app::PageDescriptor& descriptor = app::PageRegistry::Get(id);
    auto* page = descriptor.create(content_, controller_);
    page->Hide();
    contentSizer_->Add(page, 1, wxEXPAND);
    pages_[id] = page;
    page->OnAppStateChanged(controller_.Snapshot());
}

void MainWindow::ActivatePage(app::PageId id) {
    EnsurePage(id);
    pageAnimation_.reset();
    contentSizer_->Layout();

    for (const auto& entry : pages_) {
        entry.second->Show(entry.first == id);
    }
    currentPage_ = id;
    rail_->SetSelected(app::PageRegistry::IndexOf(id));

    contentSizer_->Layout();
    content_->FitInside();
    content_->Scroll(0, 0);

    PageBase* page = pages_[id];
    page->OnAppStateChanged(controller_.Snapshot());
    UpdateChrome();

    if (ThemeManager::Instance().MotionEnabled()) {
        const wxPoint base = page->GetPosition();
        page->SetPosition(base + wxPoint(0, FromDIP(8)));
        pageAnimation_ = AnimationManager::Instance().Animate(
            0.0, 1.0, motion::PageMs, motion::Enter, [this, page, base](double value) {
                page->SetPosition(
                    wxPoint(base.x, base.y + static_cast<int>(FromDIP(8) * (1.0 - value))));
            });
    }
}

void MainWindow::OnNavigate(int index) {
    const auto& pages = app::PageRegistry::All();
    if (index < 0 || index >= static_cast<int>(pages.size())) {
        return;
    }
    controller_.Navigate(pages[index].id);
}

void MainWindow::OnAppStateChanged(const app::AppStateSnapshot& state) {
    if (state.currentPage != currentPage_) {
        ActivatePage(state.currentPage);
        return;
    }
    UpdateChrome();
}

void MainWindow::UpdateChrome() {
    const app::PageDescriptor& descriptor = app::PageRegistry::Get(currentPage_);
    if (pageTitle_ != nullptr) {
        pageTitle_->SetText(descriptor.title);
        pageSubtitle_->SetText(descriptor.description);
        // Text changes invalidate the header height; re-run layout so the
        // subtitle is never clipped after a page switch.
        header_->InvalidateBestSize();
        if (root_ != nullptr) {
            root_->Layout();
        }
        header_->Layout();
    }
    if (status_ != nullptr) {
        const app::AppStateSnapshot& state = controller_.Snapshot();
        status_->SetStatus(state.kernel, state.api, state.mihomoVersion, state.proxyMode);
    }
}

void MainWindow::UpdateResponsiveRail() {
    if (rail_ == nullptr) {
        return;
    }
    const bool extended = GetClientSize().x >= FromDIP(1000);
    if (extended == railExtended_) {
        return;
    }
    railExtended_ = extended;
    rail_->SetExtended(extended);
    rail_->InvalidateBestSize();
    if (root_ != nullptr) {
        root_->Layout();
    }
    Layout();
}

void MainWindow::OnSize(wxSizeEvent& event) {
    UpdateResponsiveRail();
    event.Skip();
}

void MainWindow::OnThemeChanged() {
    const ColorScheme& colors = ThemeManager::Instance().Colors();
    SetBackgroundColour(ToWx(colors.surface));
    if (root_ != nullptr) {
        root_->SetBackgroundColour(ToWx(colors.surface));
        root_->Refresh();
    }
    if (header_ != nullptr) {
        header_->SetBackgroundColour(ToWx(colors.surface));
        header_->Refresh();
    }
    if (content_ != nullptr) {
        content_->SetBackgroundColour(ToWx(colors.surface));
        content_->Refresh();
    }
    Refresh();
}

void MainWindow::ShowMessage(const wxString& message) {
    Snackbar* snackbar = Snackbar::Present(this, message, "", {}, 5000);
    if (snackbar != nullptr) {
        // Keep the snackbar above the status bar (44 DIP + 8 DIP gap).
        snackbar->SetBottomInset(52);
    }
}

void MainWindow::ShowInfoDialog(const wxString& title, const wxString& body) {
    Dialog dialog(this, title, body);
    dialog.AddAction("Close", ButtonVariant::Filled, wxID_OK);
    dialog.ShowModal();
}

}  // namespace clashyuka::ui
