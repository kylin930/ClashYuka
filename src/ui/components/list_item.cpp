#include "clashyuka/ui/components/list_item.h"

#include <algorithm>
#include <utility>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kLeadingSizeDp = 24;
constexpr int kChevronSizeDp = 18;

Color SelectedColor(const ColorScheme& colors, double amount) {
    return Lerp(colors.onSurface, colors.primary, amount);
}

}  // namespace

ListItem::ListItem(wxWindow* parent)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS) {
    SetMinSize(DoGetBestClientSize());
    SetCursor(wxCursor(wxCURSOR_HAND));

    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        if (IsEnabled()) {
            UpdateHover(1.0);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        UpdateHover(0.0);
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (!IsEnabled()) {
            return;
        }
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
        const bool inside = GetClientRect().Contains(event.GetPosition());
        if (HasCapture()) {
            ReleaseMouse();
        }
        Refresh(false);
        if (wasPressed && inside) {
            EmitClick();
        }
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        pressed_ = false;
        UpdatePress(0.0);
        Refresh(false);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_SPACE) {
            const wxSize size = GetClientSize();
            ripple_.Trigger(wxPoint(size.x / 2, size.y / 2), size, [this] { Refresh(false); });
            pressProgress_ = 1.0;
            UpdatePress(0.0);
            EmitClick();
            return;
        }
        event.Skip();
    });
}

void ListItem::SetTitle(const wxString& title) {
    title_ = title;
    Refresh();
}

void ListItem::SetSubtitle(const wxString& subtitle) {
    subtitle_ = subtitle;
    SetMinSize(DoGetBestClientSize());
    InvalidateBestSize();
    Refresh();
}

void ListItem::SetLeadingIcon(std::optional<IconId> icon) {
    leadingIcon_ = icon;
    Refresh();
}

void ListItem::SetTrailingText(const wxString& text) {
    trailingText_ = text;
    Refresh();
}

void ListItem::SetShowChevron(bool show) {
    showChevron_ = show;
    Refresh();
}

void ListItem::SetShowDivider(bool show) {
    showDivider_ = show;
    Refresh();
}

void ListItem::SetSelected(bool selected, bool animate) {
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    if (!animate) {
        selectedAnimation_.reset();
        selectedProgress_ = selected ? 1.0 : 0.0;
        Refresh();
        return;
    }
    UpdateSelected(selected ? 1.0 : 0.0);
}

void ListItem::SetOnClick(std::function<void()> handler) {
    onClick_ = std::move(handler);
}

wxSize ListItem::DoGetBestClientSize() const {
    return wxSize(FromDIP(280), FromDIP(subtitle_.empty() ? 56 : 72));
}

bool ListItem::Enable(bool enable) {
    const bool result = wxWindow::Enable(enable);
    if (!enable) {
        pressed_ = false;
        hoverProgress_ = 0.0;
        ripple_.Cancel();
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
    SetCursor(enable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    Refresh();
    return result;
}

void ListItem::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

void ListItem::UpdatePress(double target) {
    pressAnimation_ = AnimationManager::Instance().Animate(
        pressProgress_, target, target > pressProgress_ ? motion::PressMs : motion::ReleaseMs,
        motion::Standard, [this](double value) {
            pressProgress_ = value;
            Refresh(false);
        });
}

void ListItem::UpdateSelected(double target) {
    selectedAnimation_ = AnimationManager::Instance().Animate(
        selectedProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            selectedProgress_ = value;
            Refresh(false);
        });
}

void ListItem::EmitClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
    if (onClick_) {
        onClick_();
    }
}

void ListItem::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const bool enabled = IsEnabled();

    if (selectedProgress_ > 0.01) {
        painter.FillRoundedRect(
            wxRect(0, 0, size.x, size.y), FromDIP(8),
            WithAlpha(colors.secondaryContainer,
                      static_cast<std::uint8_t>(200 * selectedProgress_)));
    }
    const double hoverAlpha = 10.0 * hoverProgress_ + 14.0 * pressProgress_;
    if (hoverAlpha > 0.01) {
        painter.FillRoundedRect(
            wxRect(0, 0, size.x, size.y), FromDIP(8),
            WithAlpha(colors.onSurface, static_cast<std::uint8_t>(hoverAlpha)));
    }
    if (enabled) {
        ripple_.Paint(painter, size, FromDIP(8), colors.onSurface);
    }

    const int padding = FromDIP(16);
    int textX = padding;
    const int centerY = size.y / 2;
    const Color accent = SelectedColor(colors, selectedProgress_);
    const Color base = enabled ? accent : WithAlpha(colors.onSurface, 90);

    if (leadingIcon_.has_value()) {
        const int iconSize = FromDIP(kLeadingSizeDp);
        IconProvider::Draw(painter, *leadingIcon_,
                           wxRect(padding, centerY - iconSize / 2, iconSize, iconSize),
                           enabled ? WithAlpha(accent, 210) : WithAlpha(colors.onSurface, 80),
                           1.8);
        textX += iconSize + FromDIP(16);
    }

    int trailingWidth = 0;
    if (!trailingText_.empty()) {
        const wxFont font = Font(TypeStyle::Label);
        const wxSize textSize = painter.MeasureText(trailingText_.ToStdString(), font);
        trailingWidth = textSize.x + FromDIP(12);
        painter.DrawText(trailingText_.ToStdString(),
                         wxRect(size.x - padding - trailingWidth, 0, trailingWidth, size.y), font,
                         WithAlpha(accent, 170), wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
    }
    if (showChevron_) {
        const int chevron = FromDIP(kChevronSizeDp);
        IconProvider::Draw(painter, IconId::ChevronRight,
                           wxRect(size.x - padding - chevron, centerY - chevron / 2, chevron,
                                  chevron),
                           WithAlpha(accent, 130), 1.8);
        trailingWidth += chevron + FromDIP(4);
    }

    const int textWidth = std::max(0, size.x - textX - padding - trailingWidth);
    if (subtitle_.empty()) {
        painter.DrawText(title_.ToStdString(), wxRect(textX, 0, textWidth, size.y),
                         Font(TypeStyle::Body), base,
                         wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    } else {
        const int top = FromDIP(12);
        painter.DrawText(title_.ToStdString(), wxRect(textX, top, textWidth, FromDIP(24)),
                         Font(TypeStyle::Body), base,
                         wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        painter.DrawText(subtitle_.ToStdString(),
                         wxRect(textX, top + FromDIP(24), textWidth, FromDIP(20)),
                         Font(TypeStyle::Label), WithAlpha(accent, 170),
                         wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    }

    if (showDivider_) {
        painter.DrawLine(textX, size.y - 1, size.x - padding, size.y - 1,
                         colors.outlineVariant, 1.0);
    }
}

}  // namespace clashyuka::ui
