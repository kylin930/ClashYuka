#include "clashyuka/ui/components/icon_button.h"

#include <algorithm>
#include <utility>

#include <wx/mousestate.h>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kSizeDp = 40;
constexpr int kIconDp = 20;

struct IconColors {
    Color background;
    Color foreground;
    Color border;
    bool hasBorder = false;
};

IconColors ResolveColors(IconButtonVariant variant, const ColorScheme& colors, bool enabled) {
    IconColors result;
    if (!enabled) {
        result.foreground = WithAlpha(colors.onSurface, 100);
        result.border = WithAlpha(colors.onSurface, 30);
        if (variant == IconButtonVariant::Filled ||
            variant == IconButtonVariant::FilledTonal) {
            result.background = WithAlpha(colors.onSurface, 30);
        }
        result.hasBorder = variant == IconButtonVariant::Outlined;
        return result;
    }

    switch (variant) {
        case IconButtonVariant::Standard:
            result.background = WithAlpha(colors.surface, 0);
            result.foreground = WithAlpha(colors.onSurface, 190);
            break;
        case IconButtonVariant::Filled:
            result.background = colors.primary;
            result.foreground = colors.onPrimary;
            break;
        case IconButtonVariant::FilledTonal:
            result.background = colors.secondaryContainer;
            result.foreground = colors.onSecondaryContainer;
            break;
        case IconButtonVariant::Outlined:
            result.background = WithAlpha(colors.surface, 0);
            result.foreground = colors.onSurface;
            result.border = colors.outlineVariant;
            result.hasBorder = true;
            break;
    }
    return result;
}

}  // namespace

IconButton::IconButton(wxWindow* parent, IconId icon, IconButtonVariant variant)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
      icon_(icon),
      variant_(variant) {
    SetMinSize(DoGetBestClientSize());
    SetCursor(wxCursor(wxCURSOR_HAND));

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

void IconButton::SetIcon(IconId icon) {
    icon_ = icon;
    Refresh();
}

void IconButton::SetVariant(IconButtonVariant variant) {
    variant_ = variant;
    Refresh();
}

void IconButton::SetOnClick(std::function<void()> handler) {
    onClick_ = std::move(handler);
}

wxSize IconButton::DoGetBestClientSize() const {
    return wxSize(FromDIP(kSizeDp), FromDIP(kSizeDp));
}

bool IconButton::Enable(bool enable) {
    const bool result = wxWindow::Enable(enable);
    if (!enable) {
        pressed_ = false;
        mouseInside_ = false;
        hoverProgress_ = 0.0;
        ripple_.Cancel();
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
    UpdateHover(enable ? 0.0 : 0.0);
    UpdateDisabled(enable ? 0.0 : 1.0);
    SetCursor(enable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    Refresh();
    return result;
}

void IconButton::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

void IconButton::UpdatePress(double target) {
    pressAnimation_ = AnimationManager::Instance().Animate(
        pressProgress_, target, target > pressProgress_ ? motion::PressMs : motion::ReleaseMs,
        motion::Standard, [this](double value) {
            pressProgress_ = value;
            Refresh(false);
        });
}

void IconButton::UpdateFocus(double target) {
    focusAnimation_ = AnimationManager::Instance().Animate(
        focusProgress_, target, motion::FocusMs, motion::Standard, [this](double value) {
            focusProgress_ = value;
            Refresh(false);
        });
}

void IconButton::UpdateDisabled(double target) {
    disabledAnimation_ = AnimationManager::Instance().Animate(
        disabledProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            disabledProgress_ = value;
            Refresh(false);
        });
}

void IconButton::EmitClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
    if (onClick_) {
        onClick_();
    }
}

void IconButton::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const bool enabled = IsEnabled();
    const bool interactive = enabled && disabledProgress_ < 0.01;

    const IconColors normal = ResolveColors(variant_, colors, true);
    const IconColors disabled = ResolveColors(variant_, colors, false);
    const double disabledMix = std::clamp(disabledProgress_, 0.0, 1.0);
    Color background = Lerp(normal.background, disabled.background, disabledMix);
    Color foreground = Lerp(normal.foreground, disabled.foreground, disabledMix);
    Color border = Lerp(normal.border, disabled.border, disabledMix);

    // MD3 state layers: hover 8%, keyboard focus 10%. No focus outline ring.
    const double stateLayer = std::max(0.08 * hoverProgress_, 0.10 * focusProgress_);
    if (interactive && stateLayer > 0.0) {
        switch (variant_) {
            case IconButtonVariant::Standard:
            case IconButtonVariant::Outlined:
                background = Lerp(colors.surface, colors.onSurface, stateLayer);
                break;
            case IconButtonVariant::Filled:
                background = Lerp(background, colors.onPrimary, stateLayer);
                break;
            case IconButtonVariant::FilledTonal:
                background = Lerp(background, colors.onSecondaryContainer, stateLayer);
                break;
        }
    }
    if (interactive && pressed_) {
        if (variant_ == IconButtonVariant::Filled) {
            background = Lerp(background, colors.onPrimary, 0.12);
        } else if (variant_ == IconButtonVariant::FilledTonal) {
            background = Lerp(background, colors.onSecondaryContainer, 0.12);
        } else {
            background = Lerp(background, colors.onSurface, 0.12);
        }
    }

    const double scale = 1.0 - 0.04 * std::clamp(pressProgress_, 0.0, 1.0);
    const int inset = static_cast<int>(size.x * (1.0 - scale) / 2.0);
    const wxRect drawRect(inset, inset, std::max(1, size.x - inset * 2),
                          std::max(1, size.y - inset * 2));
    const double radius = drawRect.height / 2.0;

    if (background.a > 0) {
        painter.FillRoundedRect(drawRect, radius, background);
    }
    if (interactive) {
        ripple_.Paint(painter, size, radius, foreground);
    }
    if (normal.hasBorder || disabled.hasBorder) {
        painter.StrokeRoundedRect(wxRect(drawRect.x + 1, drawRect.y + 1, drawRect.width - 2,
                                         drawRect.height - 2),
                                  radius, border, 1.0);
    }

    const int iconSize = FromDIP(kIconDp);
    IconProvider::Draw(painter, icon_,
                       wxRect(drawRect.x + (drawRect.width - iconSize) / 2,
                              drawRect.y + (drawRect.height - iconSize) / 2, iconSize, iconSize),
                       foreground, 1.8);
}

}  // namespace clashyuka::ui
