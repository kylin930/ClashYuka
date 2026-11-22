#pragma once

#include <map>
#include <memory>

#include <wx/frame.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/app/app_state.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/theme/theme_manager.h"

class wxBoxSizer;
class wxPanel;
class wxScrolledWindow;
class wxSizeEvent;

namespace clashyuka::ui {

class IconButton;
class NavigationRail;
class PageBase;
class StatusBarPanel;
class TextBlock;

// Root application window: navigation rail, page header, page container and
// status bar. Implements the shell services used by the controller.
class MainWindow final : public wxFrame,
                         public app::AppShell,
                         public app::AppStateObserver,
                         public ThemeObserver {
public:
    MainWindow();
    ~MainWindow() override;

    void ShowMessage(const wxString& message) override;
    void ShowInfoDialog(const wxString& title, const wxString& body) override;

    void OnAppStateChanged(const app::AppStateSnapshot& state) override;
    void OnThemeChanged() override;

private:
    void BuildUi();
    void EnsurePage(app::PageId id);
    void ActivatePage(app::PageId id);
    void OnNavigate(int index);
    void UpdateChrome();
    void UpdateResponsiveRail();
    void OnSize(wxSizeEvent& event);

    app::AppController controller_;

    wxPanel* root_ = nullptr;
    NavigationRail* rail_ = nullptr;
    wxPanel* header_ = nullptr;
    TextBlock* pageTitle_ = nullptr;
    TextBlock* pageSubtitle_ = nullptr;
    wxScrolledWindow* content_ = nullptr;
    wxBoxSizer* contentSizer_ = nullptr;
    StatusBarPanel* status_ = nullptr;

    std::map<app::PageId, PageBase*> pages_;
    std::shared_ptr<AnimationHandle> pageAnimation_;
    app::PageId currentPage_ = app::PageId::Overview;
    bool railExtended_ = true;
};

}  // namespace clashyuka::ui
