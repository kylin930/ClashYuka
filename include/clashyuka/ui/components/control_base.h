#pragma once

#include <wx/window.h>

#include "clashyuka/ui/drawing/painter.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {

// Base for owner-drawn MD3 controls: theme registration, buffered painting and
// a configurable background surface (so controls can sit on cards).
class ControlBase : public wxWindow, public ThemeObserver {
public:
    ControlBase(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size,
                long style = 0);
    ~ControlBase() override;

    void OnThemeChanged() override;

    void SetSurfaceColor(const Color& color);
    const ColorScheme& Colors() const { return ThemeManager::Instance().Colors(); }

protected:
    int Dp(int value) const;
    wxFont Font(TypeStyle style) const;
    Color Surface() const { return surface_; }
    // When false, the control paints the theme surface behind its content so
    // semi-transparent overlays (snackbar fade) blend correctly.
    void SetOpaqueBackground(bool opaque) { paintOpaque_ = opaque; }

    virtual void PaintContent(Painter& painter, const wxSize& size) = 0;

private:
    void OnPaint(wxPaintEvent& event);

    Color surface_;
    bool customSurface_ = false;
    bool paintOpaque_ = true;
};

}  // namespace clashyuka::ui
