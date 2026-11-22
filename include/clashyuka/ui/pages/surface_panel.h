#pragma once

#include <wx/dcbuffer.h>
#include <wx/panel.h>

#include "clashyuka/ui/drawing/painter.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {

// Theme-aware panel that paints the MD3 surface colour. Pages use it for
// grouping content without introducing extra cards.
class SurfacePanel : public wxPanel, public ThemeObserver {
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

}  // namespace clashyuka::ui
