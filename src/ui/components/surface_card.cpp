#include "clashyuka/ui/components/surface_card.h"

#include <wx/dcbuffer.h>
#include <wx/sizer.h>

#include "clashyuka/ui/drawing/motion_tokens.h"
#include "clashyuka/ui/drawing/painter.h"
#include "clashyuka/ui/theme/shape_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kCardPaddingDp = 16;

}  // namespace

SurfaceCard::SurfaceCard(wxWindow* parent, SurfaceLevel level)
    : wxPanel(parent), level_(level) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    ThemeManager::Instance().AddObserver(this);
    Bind(wxEVT_PAINT, &SurfaceCard::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

SurfaceCard::~SurfaceCard() {
    ThemeManager::Instance().RemoveObserver(this);
}

void SurfaceCard::SetLevel(SurfaceLevel level) {
    level_ = level;
    Refresh();
}

void SurfaceCard::SetOutlined(bool outlined) {
    outlined_ = outlined;
    Refresh();
}

void SurfaceCard::SetClickable(bool clickable) {
    if (clickable_ == clickable) {
        return;
    }
    clickable_ = clickable;
    SetCursor(clickable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));

    if (clickable) {
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
            hovered_ = true;
            UpdateHover(1.0);
        });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            hovered_ = false;
            pressed_ = false;
            UpdateHover(0.0);
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            pressed_ = true;
            UpdatePress(1.0);
            CaptureMouse();
            Refresh(false);
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            const bool wasPressed = pressed_;
            pressed_ = false;
            UpdatePress(0.0);
            if (HasCapture()) {
                ReleaseMouse();
            }
            Refresh(false);
            if (wasPressed && GetClientRect().Contains(event.GetPosition())) {
                EmitClick();
            }
        });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
            pressed_ = false;
            UpdatePress(0.0);
            Refresh(false);
        });
    }
    Refresh();
}

void SurfaceCard::SetContent(wxSizer* content, int paddingDp) {
    if (paddingDp >= 0) {
        paddingDp_ = paddingDp;
    }
    if (content == nullptr) {
        return;
    }
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(content, 1, wxEXPAND | wxALL, FromDIP(paddingDp_));
    SetSizer(outer);
    Layout();
}

void SurfaceCard::SetOnClick(std::function<void()> handler) {
    onClick_ = std::move(handler);
    SetClickable(onClick_ != nullptr);
}

void SurfaceCard::OnThemeChanged() {
    Refresh();
}

Color SurfaceCard::CardColor() const {
    const ColorScheme& colors = ThemeManager::Instance().Colors();
    switch (level_) {
        case SurfaceLevel::Lowest:
            return colors.surfaceContainerLowest;
        case SurfaceLevel::Low:
            return colors.surfaceContainerLow;
        case SurfaceLevel::Container:
            return colors.surfaceContainer;
        case SurfaceLevel::High:
            return colors.surfaceContainerHigh;
        case SurfaceLevel::Highest:
            return colors.surfaceContainerHighest;
    }
    return colors.surfaceContainer;
}

void SurfaceCard::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

void SurfaceCard::UpdatePress(double target) {
    pressAnimation_ = AnimationManager::Instance().Animate(
        pressProgress_, target, target > pressProgress_ ? motion::PressMs : motion::ReleaseMs,
        motion::Standard, [this](double value) {
            pressProgress_ = value;
            Refresh(false);
        });
}

void SurfaceCard::EmitClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
    if (onClick_) {
        onClick_();
    }
}

void SurfaceCard::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    const ColorScheme& colors = ThemeManager::Instance().Colors();

    dc.SetBackground(wxBrush(ToWx(colors.surface)));
    dc.Clear();

    auto gc = CreateGraphicsContext(dc);
    if (!gc) {
        return;
    }
    Painter painter(gc.get());

    const wxRect rect(0, 0, size.x, size.y);
    Color fill = CardColor();
    if (hoverProgress_ > 0.0) {
        fill = Lerp(fill, colors.onSurface, 0.05 * hoverProgress_);
    }
    if (pressProgress_ > 0.0) {
        fill = Lerp(fill, colors.onSurface, 0.08 * pressProgress_);
    }

    const double radius = FromDIP(ShapeDp(ShapeToken::Large));
    painter.FillRoundedRect(rect, radius, fill);
    if (outlined_) {
        painter.StrokeRoundedRect(wxRect(0, 0, size.x, size.y), radius,
                                  Lerp(colors.outlineVariant, colors.primary,
                                       0.5 * hoverProgress_),
                                  1.0);
    }
}

}  // namespace clashyuka::ui
