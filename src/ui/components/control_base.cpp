#include "clashyuka/ui/components/control_base.h"

#include <wx/dcbuffer.h>

#include "clashyuka/ui/components/surface_card.h"

namespace clashyuka::ui {

ControlBase::ControlBase(wxWindow* parent, wxWindowID id, const wxPoint& pos,
                         const wxSize& size, long style)
    : wxWindow(parent, id, pos, size, style), surface_(ThemeManager::Instance().Colors().surface) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    ThemeManager::Instance().AddObserver(this);
    Bind(wxEVT_PAINT, &ControlBase::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

ControlBase::~ControlBase() {
    ThemeManager::Instance().RemoveObserver(this);
}

void ControlBase::OnThemeChanged() {
    if (!customSurface_) {
        surface_ = Colors().surface;
    }
    Refresh();
}

void ControlBase::SetSurfaceColor(const Color& color) {
    customSurface_ = true;
    surface_ = color;
    Refresh();
}

int ControlBase::Dp(int value) const {
    return FromDIP(value);
}

wxFont ControlBase::Font(TypeStyle style) const {
    return MakeFont(ThemeManager::Instance().Type().Get(style), this);
}

void ControlBase::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    Color background = paintOpaque_ ? surface_ : Colors().surface;
    if (!customSurface_) {
        // Rows and labels placed on a SurfaceCard must use the card colour;
        // child windows cannot show the parent surface through themselves.
        // Use C++ RTTI: SurfaceCard has no wxWidgets dynamic-class macros.
        if (auto* card = dynamic_cast<SurfaceCard*>(GetParent())) {
            background = card->CardColor();
        }
    }
    dc.SetBackground(wxBrush(ToWx(background)));
    dc.Clear();
    auto gc = CreateGraphicsContext(dc);
    if (!gc) {
        return;
    }
    Painter painter(gc.get());
    PaintContent(painter, GetClientSize());
}

}  // namespace clashyuka::ui
