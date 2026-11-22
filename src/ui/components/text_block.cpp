#include "clashyuka/ui/components/text_block.h"

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {

TextBlock::TextBlock(wxWindow* parent, wxString text, TypeStyle style, int alpha)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
      text_(std::move(text)),
      style_(style),
      alpha_(alpha) {}

void TextBlock::SetText(const wxString& text) {
    if (text_ == text) {
        return;
    }
    text_ = text;
    InvalidateBestSize();
    fadeAnimation_ = AnimationManager::Instance().Animate(
        0.35, 1.0, motion::TextFadeMs, motion::Standard, [this](double value) {
            contentAlpha_ = value;
            Refresh(false);
        });
    Refresh();
}

wxSize TextBlock::DoGetBestClientSize() const {
    const wxFont font = Font(style_);
    int width = 0;
    int height = 0;
    GetTextExtent(text_, &width, &height, nullptr, nullptr, &font);
    return wxSize(width, height + FromDIP(4));
}

void TextBlock::PaintContent(Painter& painter, const wxSize& size) {
    const auto alpha = static_cast<std::uint8_t>(alpha_ * contentAlpha_);
    painter.DrawText(text_.utf8_string(), wxRect(0, 0, size.x, size.y), Font(style_),
                     WithAlpha(Colors().onSurface, alpha),
                     wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
}

}  // namespace clashyuka::ui
