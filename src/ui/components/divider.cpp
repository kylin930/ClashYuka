#include "clashyuka/ui/components/divider.h"

namespace clashyuka::ui {

Divider::Divider(wxWindow* parent, int insetDp)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1)), insetDp_(insetDp) {
    SetMinSize(wxSize(-1, Dp(1)));
}

void Divider::SetInset(int insetDp) {
    insetDp_ = insetDp;
    Refresh();
}

void Divider::PaintContent(Painter& painter, const wxSize& size) {
    const int inset = Dp(insetDp_);
    const int y = size.y / 2;
    painter.DrawLine(inset, y, size.x - inset, y, Colors().outlineVariant, 1.0);
}

}  // namespace clashyuka::ui
