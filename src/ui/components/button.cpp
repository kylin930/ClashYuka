#include "clashyuka/ui/components/button.h"

#include <algorithm>
#include <utility>

#include <wx/mousestate.h>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kButtonHeightDp = 40;
constexpr int kIconSizeDp = 18;
constexpr int kIconGapDp = 8;

}  // namespace

Button::Button(wxWindow* parent, const wxString& label, ButtonVariant variant,
               std::optional<IconId> icon)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
      label_(label),
      variant_(variant),
      icon_(icon) {
    SetMinSize(DoGetBestClientSize());
    SetCursor(wxCursor(wxCURSOR_HAND));
    BindInteraction();
}

void Button::SetLabel(const wxString& label) {
    label_ = label;
    SetMinSize(DoGetBestClientSize());
    InvalidateBestSize();
    Refresh();
}

void Button::SetVariant(ButtonVariant variant) {
    variant_ = variant;
    InvalidateBestSize();
    SetMinSize(DoGetBestClientSize());
    Refresh();
}

void Button::SetIcon(std::optional<IconId> icon) {
    icon_ = icon;
    InvalidateBestSize();
    SetMinSize(DoGetBestClientSize());
    Refresh();
}

void Button::SetOnClick(std::function<void()> handler) {
    onClick_ = std::move(handler);
}

wxSize Button::DoGetBestClientSize() const {
    const wxFont font = MakeFont(ThemeManager::Instance().Type().Get(TypeStyle::Label), this);
    int textWidth = 0;
    int textHeight = 0;
    GetTextExtent(label_, &textWidth, &textHeight, nullptr, nullptr, &font);
    const int horizontal = FromDIP(variant_ == ButtonVariant::Text ? 12 : 24) * 2;
    const int iconPart = icon_.has_value() ? FromDIP(kIconSizeDp) + FromDIP(kIconGapDp) : 0;
    return wxSize(textWidth + horizontal + iconPart, FromDIP(kButtonHeightDp));
}

bool Button::Enable(bool enable) {
    const bool result = wxWindow::Enable(enable);
    if (!enable) {
        hovered_ = false;
        pressed_ = false;
        mouseInside_ = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
        ripple_.Cancel();
    }
    UpdateDisabled(enable ? 0.0 : 1.0);
    SetCursor(enable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    Refresh();
    return result;
}

void Button::BindInteraction() {
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        mouseInside_ = true;
        if (IsEnabled()) {
            UpdateHover(1.0);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        mouseInside_ = false;
        UpdateHover(0.0);
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (!IsEnabled()) {
            return;
        }
        SetFocus();
        pressed_ = true;
        mouseInside_ = true;
        UpdatePress(1.0);
        ripple_.Trigger(event.GetPosition(), GetClientSize(), [this] { Refresh(false); });
        if (!HasCapture()) {
            CaptureMouse();
        }
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        if (!IsEnabled()) {
            return;
        }
        const bool wasPressed = pressed_;
        pressed_ = false;
        UpdatePress(0.0);
        mouseInside_ = GetClientRect().Contains(event.GetPosition());
        if (HasCapture()) {
            ReleaseMouse();
        }
        Refresh(false);
        if (wasPressed && mouseInside_) {
            EmitClick();
        }
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        pressed_ = false;
        UpdatePress(0.0);
        Refresh(false);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (!IsEnabled()) {
            event.Skip();
            return;
        }
        if (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
            const wxSize size = GetClientSize();
            ripple_.Trigger(wxPoint(size.x / 2, size.y / 2), size, [this] { Refresh(false); });
            pressProgress_ = 1.0;
            UpdatePress(0.0);
            EmitClick();
            return;
        }
        event.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        // MD3 shows a focus state layer for keyboard navigation only. Pointer
        // focus and the dialog manager's initial focus stay tint-free.
        const bool keyboard = ThemeManager::Instance().KeyboardNavigation() &&
                              !wxGetMouseState().LeftIsDown();
        UpdateFocus(keyboard ? 1.0 : 0.0);
        event.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        UpdateFocus(0.0);
        event.Skip();
    });
}

void Button::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

void Button::UpdatePress(double target) {
    pressAnimation_ = AnimationManager::Instance().Animate(
        pressProgress_, target, target > pressProgress_ ? motion::PressMs : motion::ReleaseMs,
        motion::Standard, [this](double value) {
            pressProgress_ = value;
            Refresh(false);
        });
}

void Button::UpdateFocus(double target) {
    focusAnimation_ = AnimationManager::Instance().Animate(
        focusProgress_, target, motion::FocusMs, motion::Standard, [this](double value) {
            focusProgress_ = value;
            Refresh(false);
        });
}

void Button::UpdateDisabled(double target) {
    disabledAnimation_ = AnimationManager::Instance().Animate(
        disabledProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            disabledProgress_ = value;
            Refresh(false);
        });
}

void Button::EmitClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
    if (onClick_) {
        onClick_();
    }
}

void Button::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const bool enabled = IsEnabled();
    const bool interactive = enabled && disabledProgress_ < 0.01;

    const ActionColors normal = ResolveActionColors(variant_, colors, true);
    const ActionColors disabled = ResolveActionColors(variant_, colors, false);
    const double disabledMix = std::clamp(disabledProgress_, 0.0, 1.0);
    Color background = Lerp(normal.background, disabled.background, disabledMix);
    Color foreground = Lerp(normal.foreground, disabled.foreground, disabledMix);
    Color border = Lerp(normal.border, disabled.border, disabledMix);

    if (interactive && variant_ == ButtonVariant::Outlined && hoverProgress_ > 0.0) {
        border = Lerp(border, colors.primary, hoverProgress_);
    }

    // MD3 state layers: hover 8%, keyboard focus 10%. No focus outline ring.
    const double stateLayer = std::max(0.08 * hoverProgress_, 0.10 * focusProgress_);
    if (interactive && stateLayer > 0.0) {
        if (variant_ == ButtonVariant::Filled) {
            background = Lerp(background, colors.onPrimary, stateLayer);
        } else if (variant_ == ButtonVariant::FilledTonal) {
            background = Lerp(background, colors.onSecondaryContainer, stateLayer);
        } else {
            background = Lerp(background, colors.primary, stateLayer);
        }
    }
    if (interactive && pressed_) {
        if (variant_ == ButtonVariant::Filled) {
            background = Lerp(background, colors.onPrimary, 0.12);
        } else if (variant_ == ButtonVariant::FilledTonal) {
            background = Lerp(background, colors.onSecondaryContainer, 0.12);
        } else {
            background = Lerp(background, colors.primary, 0.12);
        }
    }

    const double scale = 1.0 - 0.03 * std::clamp(pressProgress_, 0.0, 1.0);
    const int insetX = static_cast<int>(size.x * (1.0 - scale) / 2.0);
    const int insetY = static_cast<int>(size.y * (1.0 - scale) / 2.0);
    const wxRect drawRect(insetX, insetY, std::max(1, size.x - insetX * 2),
                          std::max(1, size.y - insetY * 2));
    const double radius = drawRect.height / 2.0;

    if (background.a > 0) {
        painter.FillRoundedRect(drawRect, radius, background);
    }

    if (interactive) {
        Color rippleColor = variant_ == ButtonVariant::Filled
                                ? colors.onPrimary
                                : (variant_ == ButtonVariant::FilledTonal
                                       ? colors.onSecondaryContainer
                                       : colors.primary);
        ripple_.Paint(painter, size, radius, rippleColor);
    }

    if (normal.hasBorder || disabled.hasBorder) {
        painter.StrokeRoundedRect(wxRect(drawRect.x + 1, drawRect.y + 1, drawRect.width - 2,
                                         drawRect.height - 2),
                                  radius, border, 1.0);
    }

    const wxFont font = Font(TypeStyle::Label);
    const wxSize textSize = painter.MeasureText(label_.utf8_string(), font);
    const int iconSize = FromDIP(kIconSizeDp);
    const int gap = icon_.has_value() ? FromDIP(kIconGapDp) : 0;
    const int contentWidth = textSize.x + (icon_.has_value() ? iconSize + gap : 0);
    const int startX = drawRect.x + (drawRect.width - contentWidth) / 2;
    const int centerY = drawRect.y + drawRect.height / 2;

    int textX = startX;
    if (icon_.has_value()) {
        const wxRect iconRect(startX, centerY - iconSize / 2, iconSize, iconSize);
        IconProvider::Draw(painter, *icon_, iconRect, foreground, 1.8);
        textX += iconSize + gap;
    }

    painter.DrawText(label_.utf8_string(),
                     wxRect(textX, drawRect.y, drawRect.x + drawRect.width - textX,
                            drawRect.height),
                     font, foreground, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
}

}  // namespace clashyuka::ui
