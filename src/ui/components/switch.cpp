#include "clashyuka/ui/components/switch.h"

#include <algorithm>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kWidthDp = 52;
constexpr int kHeightDp = 32;

}  // namespace

Switch::Switch(wxWindow* parent)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS) {
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
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!IsEnabled()) {
            return;
        }
        SetFocus();
        pressed_ = true;
        mouseInside_ = true;
        UpdatePress(1.0);
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
            Toggle();
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
            pressProgress_ = 1.0;
            UpdatePress(0.0);
            Toggle();
            return;
        }
        event.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        Refresh();
        event.Skip();
    });
}

wxSize Switch::DoGetBestClientSize() const {
    return wxSize(FromDIP(kWidthDp), FromDIP(kHeightDp));
}

bool Switch::Enable(bool enable) {
    const bool result = wxWindow::Enable(enable);
    if (!enable) {
        pressed_ = false;
        mouseInside_ = false;
        hoverProgress_ = 0.0;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
    UpdateDisabled(enable ? 0.0 : 1.0);
    SetCursor(enable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    Refresh();
    return result;
}

void Switch::SetValue(bool value, bool animate) {
    if (value_ == value) {
        return;
    }
    value_ = value;
    if (!animate) {
        animation_.reset();
        progress_ = value ? 1.0 : 0.0;
        Refresh();
        return;
    }
    AnimateTo(value ? 1.0 : 0.0);
}

void Switch::SetOnChange(std::function<void(bool)> handler) {
    onChange_ = std::move(handler);
}

void Switch::AnimateTo(double target) {
    animation_ = AnimationManager::Instance().Animate(
        progress_, target, motion::SwitchMs, motion::Balanced, [this](double value) {
            progress_ = value;
            Refresh(false);
        });
}

void Switch::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

void Switch::UpdatePress(double target) {
    pressAnimation_ = AnimationManager::Instance().Animate(
        pressProgress_, target, target > pressProgress_ ? motion::PressMs : motion::ReleaseMs,
        motion::Standard, [this](double value) {
            pressProgress_ = value;
            Refresh(false);
        });
}

void Switch::UpdateDisabled(double target) {
    disabledAnimation_ = AnimationManager::Instance().Animate(
        disabledProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            disabledProgress_ = value;
            Refresh(false);
        });
}

void Switch::Toggle() {
    if (!IsEnabled()) {
        return;
    }
    value_ = !value_;
    AnimateTo(value_ ? 1.0 : 0.0);
    wxCommandEvent event(wxEVT_CHECKBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(value_ ? 1 : 0);
    ProcessWindowEvent(event);
    if (onChange_) {
        onChange_(value_);
    }
}

void Switch::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const double disabledMix = std::clamp(disabledProgress_, 0.0, 1.0);

    Color track = Lerp(colors.surfaceContainerHighest, colors.primary, progress_);
    Color thumb = Lerp(colors.outline, colors.onPrimary, progress_);
    track = Lerp(track, WithAlpha(colors.onSurface, 30), disabledMix);
    thumb = Lerp(thumb, WithAlpha(colors.onSurface, 100), disabledMix);

    const double radius = size.y / 2.0;
    painter.FillRoundedRect(wxRect(0, 0, size.x, size.y), radius, track);
    if (progress_ < 0.99 && disabledMix < 0.5) {
        painter.StrokeRoundedRect(wxRect(1, 1, size.x - 2, size.y - 2), radius,
                                  Lerp(colors.outline, colors.primary, 0.25 * hoverProgress_),
                                  1.0);
    }

    const double centerX = FromDIP(16) + (FromDIP(32) - FromDIP(16)) * progress_;
    if (hoverProgress_ > 0.01 && disabledMix < 0.5) {
        const double layer = size.y / 2.0 + FromDIP(4);
        painter.FillCircle(
            centerX, size.y / 2.0, layer,
            WithAlpha(colors.onSurface, static_cast<std::uint8_t>(18 * hoverProgress_)));
    }

    const double thumbRadius =
        (FromDIP(16) + FromDIP(8) * progress_ + FromDIP(4) * pressProgress_) / 2.0;
    painter.FillCircle(centerX, size.y / 2.0, thumbRadius, thumb);
}

}  // namespace clashyuka::ui
