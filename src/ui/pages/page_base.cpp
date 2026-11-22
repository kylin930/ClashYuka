#include "clashyuka/ui/pages/page_base.h"

#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/drawing/painter.h"

namespace clashyuka::ui {

PageBase::PageBase(wxWindow* parent, app::AppController& controller, app::PageId id)
    : wxPanel(parent), controller_(controller), id_(id) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    Bind(wxEVT_PAINT, &PageBase::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    ThemeManager::Instance().AddObserver(this);
    controller_.State().AddObserver(this);
}

PageBase::~PageBase() {
    controller_.State().RemoveObserver(this);
    ThemeManager::Instance().RemoveObserver(this);
}

const app::AppStateSnapshot& PageBase::State() const {
    return controller_.Snapshot();
}

void PageBase::OnAppStateChanged(const app::AppStateSnapshot& snapshot) {
    ApplyState(snapshot);
}

void PageBase::OnThemeChanged() {
    Refresh();
}

void PageBase::SyncState() {
    ApplyState(controller_.Snapshot());
}

void PageBase::RequestRelayout() {
    Layout();
    for (wxWindow* window = GetParent(); window != nullptr; window = window->GetParent()) {
        if (auto* scrolled = wxDynamicCast(window, wxScrolledWindow)) {
            scrolled->Layout();
            scrolled->FitInside();
            break;
        }
    }
}

void PageBase::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(ToWx(ThemeManager::Instance().Colors().surface)));
    dc.Clear();
}

}  // namespace clashyuka::ui
