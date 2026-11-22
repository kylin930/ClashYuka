#pragma once

#include <memory>

#include <wx/string.h>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

// Single-line theme-aware text using the typography tokens.
class TextBlock final : public ControlBase {
public:
    TextBlock(wxWindow* parent, wxString text, TypeStyle style, int alpha = 255);

    void SetText(const wxString& text);
    // Horizontal alignment inside the window rect (wxALIGN_LEFT by default).
    void SetAlignment(int alignment);
    wxSize DoGetBestClientSize() const override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    wxString text_;
    TypeStyle style_;
    int alpha_;
    int alignment_ = wxALIGN_LEFT;
    double contentAlpha_ = 1.0;
    std::shared_ptr<AnimationHandle> fadeAnimation_;
};

}  // namespace clashyuka::ui
