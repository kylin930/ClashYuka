#pragma once

#include <wx/panel.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::app {
class AppController;
}

namespace clashyuka::ui {

// Base class for every application page. Pages observe application state (read
// only) and use the controller for navigation, theme changes and dialogs.
class PageBase : public wxPanel, public app::AppStateObserver, public ThemeObserver {
public:
    PageBase(wxWindow* parent, app::AppController& controller, app::PageId id);
    ~PageBase() override;

    app::PageId Id() const { return id_; }
    app::AppController& Controller() const { return controller_; }
    const app::AppStateSnapshot& State() const;

    void OnAppStateChanged(const app::AppStateSnapshot& state) final;
    void OnThemeChanged() override;

protected:
    // Override to reflect state changes in page widgets.
    virtual void ApplyState(const app::AppStateSnapshot&) {}
    // Re-applies the current snapshot (used right after page creation).
    void SyncState();
    void RequestRelayout();

private:
    void OnPaint(wxPaintEvent& event);

    app::AppController& controller_;
    app::PageId id_;
};

}  // namespace clashyuka::ui
