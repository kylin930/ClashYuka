#include "clashyuka/ui/main_window.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/weakref.h>

#ifdef __WXMSW__
#include <wx/artprov.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msw/wrapwin.h>
#include <wx/taskbar.h>
#endif

#include "clashyuka/app/app_text.h"
#include "clashyuka/app/page_registry.h"
#include "clashyuka/core/timing.h"
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

#ifdef __WXMSW__
namespace {
constexpr int kHotkeyFirst = 1;
constexpr int kHotkeySecond = 2;
constexpr int kTrayProxy = wxID_HIGHEST + 1;

UINT TaskbarCreatedMessage() {
    static const UINT message = ::RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}
}  // namespace

class DesktopTrayIcon final : public wxTaskBarIcon {
public:
    explicit DesktopTrayIcon(MainWindow& window) : window_(window) {
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, [this](wxTaskBarIconEvent&) { window_.RestoreWindow(); });
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { window_.RestoreWindow(); }, wxID_OPEN);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            auto& controller = window_.controller_;
            if (controller.SystemProxyPreferenceAvailable()) {
                controller.SetSystemProxyEnabled(!controller.Snapshot().systemProxyEnabled);
            }
        }, kTrayProxy);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            // Let the native menu finish with its temporary event handler first.
            wxWeakRef<MainWindow> window(&window_);
            window_.CallAfter([window] {
                if (window && !window->closing_) {
                    window->Close(true);
                }
            });
        }, wxID_EXIT);
    }

protected:
    wxMenu* CreatePopupMenu() override {
        using app::DesktopText;
        auto* menu = new wxMenu;
        menu->Append(wxID_OPEN, DesktopText("Open ClashYuka", "打开 ClashYuka"));
        const auto& controller = window_.controller_;
        auto* proxy = menu->AppendCheckItem(kTrayProxy,
                                            DesktopText("System proxy preference", "系统代理开关"));
        proxy->Check(controller.Snapshot().systemProxyEnabled);
        proxy->Enable(controller.SystemProxyPreferenceAvailable());
        menu->AppendSeparator();
        menu->Append(wxID_EXIT, DesktopText("Exit ClashYuka", "退出 ClashYuka"));
        return menu;
    }

private:
    MainWindow& window_;
};
#endif

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
            case app::KernelState::WaitingForApi:
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
    core::MarkStartup("main-window-constructed");

    ThemeManager::Instance().AddObserver(this);
    controller_.State().AddObserver(this);

    BuildUi();
    Bind(wxEVT_SIZE, &MainWindow::OnSize, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainWindow::OnClose, this);
    SetMinClientSize(FromDIP(wxSize(880, 600)));
    SetClientSize(FromDIP(wxSize(1100, 720)));
    Centre();

    ActivatePage(controller_.Snapshot().currentPage);
    UpdateResponsiveRail();

    // The window is shown before the kernel is initialized so a slow or failing
    // Mihomo start never delays the first paint. The weak reference keeps a
    // close-before-idle sequence from calling into a destroyed window.
    wxWeakRef<MainWindow> self(this);
    CallAfter([self] {
        if (!self || self->closing_) {
            return;
        }
        core::MarkStartup("main-window-shown");
#ifdef __WXMSW__
        self->EnsureTrayIcon();
#endif
        self->controller_.InitializeRuntime();
    });
}

MainWindow::~MainWindow() {
    closing_ = true;
#ifdef __WXMSW__
    StopDesktopControls();
#endif
    controller_.ShutdownRuntime();
    AnimationManager::Instance().StopAll();
    controller_.State().RemoveObserver(this);
    ThemeManager::Instance().RemoveObserver(this);
    // Pages unregister their observers while the controller still exists.
    DestroyChildren();
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

void MainWindow::OnClose(wxCloseEvent& event) {
#ifdef __WXMSW__
    if (!closing_ && event.CanVeto() && controller_.Settings().closeToTray && EnsureTrayIcon()) {
        event.Veto();
        Hide();
        return;
    }
#endif
    closing_ = true;
#ifdef __WXMSW__
    StopDesktopControls();
#endif
    controller_.ShutdownRuntime();
    event.Skip();
}

#ifdef __WXMSW__
bool MainWindow::EnsureTrayIcon() {
    if (closing_ || !wxTaskBarIcon::IsAvailable()) {
        return false;
    }
    if (!tray_) {
        tray_ = std::make_unique<DesktopTrayIcon>(*this);
    }
    const auto icon = wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(16, 16));
    if (!icon.IsOk()) {
        return false;
    }
    // SetIcon verifies the shell still has the icon. IsIconInstalled only
    // reflects wxWidgets' cached state and can be stale after Explorer restarts.
    const wxLogNull silence;
    if (tray_->SetIcon(icon, "ClashYuka")) {
        return true;
    }
    if (tray_->IsIconInstalled()) {
        tray_->RemoveIcon();
        return tray_->SetIcon(icon, "ClashYuka");
    }
    return false;
}

void MainWindow::RestoreWindow() {
    if (!closing_) {
        Show();
        Iconize(false);
        Raise();
    }
}

void MainWindow::ToggleWindow() {
    if (closing_ || !IsEnabled()) {
        return;
    }
    if (!IsShown() || IsIconized() || !IsActive()) {
        RestoreWindow();
    } else if (EnsureTrayIcon()) {
        Hide();
    } else {
        // Keep a taskbar entry when no tray icon can restore the window.
        Iconize();
    }
}

bool MainWindow::PrepareWindowHotkey(const runtime::WindowHotkey& hotkey) {
    if (closing_) {
        return false;
    }
    if (!hotkey.key) {
        return true;
    }
    const int id = activeHotkeyId_ == kHotkeyFirst ? kHotkeySecond : kHotkeyFirst;
    UINT modifiers = MOD_NOREPEAT;
    if (hotkey.ctrl) modifiers |= MOD_CONTROL;
    if (hotkey.alt) modifiers |= MOD_ALT;
    if (hotkey.shift) modifiers |= MOD_SHIFT;
    if (hotkey.win) modifiers |= MOD_WIN;
    if (!::RegisterHotKey(static_cast<HWND>(GetHandle()), id, modifiers, static_cast<UINT>(hotkey.key))) {
        return false;
    }
    pendingHotkeyId_ = id;
    return true;
}

void MainWindow::FinishWindowHotkey(bool commit) {
    const int releaseId = commit ? activeHotkeyId_ : pendingHotkeyId_;
    if (releaseId) {
        ::UnregisterHotKey(static_cast<HWND>(GetHandle()), releaseId);
    }
    if (commit) {
        activeHotkeyId_ = pendingHotkeyId_;
    }
    pendingHotkeyId_ = 0;
}

void MainWindow::StopDesktopControls() {
    FinishWindowHotkey(false);
    FinishWindowHotkey(true);
    const wxLogNull silence;
    tray_.reset();
}

WXLRESULT MainWindow::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    if (message == WM_HOTKEY && activeHotkeyId_ != 0 &&
        wParam == static_cast<WXWPARAM>(activeHotkeyId_)) {
        ToggleWindow();
        return 0;
    }
    if (!closing_ && message != 0 && message == TaskbarCreatedMessage()) {
        // Give wxTaskBarIcon a chance to handle the same broadcast first.
        CallAfter([this] {
            if (!closing_ && !EnsureTrayIcon() && !IsShown()) {
                RestoreWindow();
            }
        });
    }
    return wxFrame::MSWWindowProc(message, wParam, lParam);
}
#endif

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

bool MainWindow::ShowConfirmDialog(const wxString& title, const wxString& body,
                                   const wxString& confirmLabel) {
    Dialog dialog(this, title, body);
    dialog.AddAction("Cancel", ButtonVariant::Text, wxID_CANCEL);
    dialog.AddAction(confirmLabel, ButtonVariant::Filled, wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}

}  // namespace clashyuka::ui
