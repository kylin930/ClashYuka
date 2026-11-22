#include "clashyuka/ui/components/navigation_rail.h"

#include <algorithm>
#include <utility>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kItemHeightDp = 56;
constexpr int kIconDp = 24;
constexpr int kGapDp = 4;

}  // namespace

NavigationRail::NavigationRail(wxWindow* parent, bool extended)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
      extended_(extended) {
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const int index = HitTest(event.GetPosition());
        if (index != hovered_) {
            hovered_ = index;
            hoverProgress_ = 0.0;
            UpdateHover(index >= 0 ? 1.0 : 0.0);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hovered_ = -1;
        UpdateHover(0.0);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const int index = HitTest(event.GetPosition());
        if (index >= 0 && index < static_cast<int>(items_.size()) && index != selected_) {
            SetSelected(index);
            if (onSelect_) {
                onSelect_(index);
            }
            wxCommandEvent command(wxEVT_BUTTON, GetId());
            command.SetEventObject(this);
            command.SetInt(index);
            ProcessWindowEvent(command);
        }
    });
}

void NavigationRail::SetItems(std::vector<NavigationRailItem> items) {
    items_ = std::move(items);
    if (selected_ >= static_cast<int>(items_.size())) {
        selected_ = items_.empty() ? -1 : 0;
    }
    if (selected_ < 0 && !items_.empty()) {
        selected_ = 0;
    }
    previousSelected_ = selected_;
    selectionBlend_ = 1.0;
    SnapIndicator();
    SetMinSize(DoGetBestClientSize());
    InvalidateBestSize();

    const bool hasBadge = std::any_of(items_.begin(), items_.end(),
                                      [](const NavigationRailItem& item) { return item.badge > 0; });
    if (hasBadge) {
        badgeProgress_ = 0.0;
        badgeAnimation_ = AnimationManager::Instance().Animate(
            0.0, 1.0, motion::BadgeMs, motion::Soft, [this](double value) {
                badgeProgress_ = value;
                Refresh(false);
            });
    } else {
        badgeProgress_ = 1.0;
    }
    Refresh();
}

void NavigationRail::SetSelected(int index) {
    if (index < -1 || index >= static_cast<int>(items_.size()) || index == selected_) {
        return;
    }
    previousSelected_ = selected_;
    selected_ = index;
    if (selected_ < 0) {
        return;
    }

    const double targetY = static_cast<double>(PillTop(selected_));
    indicatorAnimation_ = AnimationManager::Instance().Animate(
        indicatorY_, targetY, motion::RailMs, motion::Standard, [this](double value) {
            indicatorY_ = value;
            Refresh(false);
        });

    selectionBlend_ = 0.0;
    blendAnimation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::RailMs, motion::Standard, [this](double value) {
            selectionBlend_ = value;
            Refresh(false);
        });
    Refresh(false);
}

void NavigationRail::SetExtended(bool extended, bool animate) {
    if (extended_ == extended) {
        return;
    }
    extended_ = extended;
    SetMinSize(DoGetBestClientSize());
    InvalidateBestSize();
    SnapIndicator();

    if (!animate) {
        labelAlpha_ = 1.0;
        Refresh();
        return;
    }
    labelAlpha_ = 0.0;
    labelAnimation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::RailMs, motion::Standard, [this](double value) {
            labelAlpha_ = value;
            Refresh(false);
        });
}

void NavigationRail::SetOnSelect(std::function<void(int)> handler) {
    onSelect_ = std::move(handler);
}

wxSize NavigationRail::DoGetBestClientSize() const {
    const int width = extended_ ? 200 : 80;
    return wxSize(FromDIP(width), FromDIP(16) + static_cast<int>(items_.size()) *
                                                    (ItemHeightPx() + FromDIP(kGapDp)));
}

int NavigationRail::ItemHeightPx() const {
    return FromDIP(kItemHeightDp);
}

int NavigationRail::PillHeightPx() const {
    return FromDIP(extended_ ? 40 : 32);
}

int NavigationRail::ItemTop(int index) const {
    return FromDIP(8) + index * (ItemHeightPx() + FromDIP(kGapDp));
}

int NavigationRail::PillTop(int index) const {
    return ItemTop(index) + (ItemHeightPx() - PillHeightPx()) / 2;
}

void NavigationRail::SnapIndicator() {
    if (selected_ >= 0) {
        indicatorY_ = static_cast<double>(PillTop(selected_));
    }
}

void NavigationRail::UpdateHover(double target) {
    hoverAnimation_ = AnimationManager::Instance().Animate(
        hoverProgress_, target, motion::HoverMs, motion::Standard, [this](double value) {
            hoverProgress_ = value;
            Refresh(false);
        });
}

int NavigationRail::HitTest(const wxPoint& point) const {
    const int cellHeight = ItemHeightPx() + FromDIP(kGapDp);
    if (point.x < FromDIP(8) || point.x > GetClientSize().x - FromDIP(8)) {
        return -1;
    }
    const int relative = point.y - FromDIP(8);
    if (relative < 0) {
        return -1;
    }
    const int index = relative / cellHeight;
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return -1;
    }
    return index;
}

void NavigationRail::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const int cellX = FromDIP(8);
    const int cellWidth = size.x - FromDIP(16);
    const int cellHeight = ItemHeightPx();
    const int pillHeight = PillHeightPx();
    const int iconSize = FromDIP(kIconDp);

    if (selected_ >= 0 && selected_ < static_cast<int>(items_.size()) && selectionBlend_ > 0.0) {
        painter.FillRoundedRect(
            wxRect(cellX, static_cast<int>(indicatorY_), cellWidth, pillHeight),
            pillHeight / 2.0, colors.secondaryContainer);
    }

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const NavigationRailItem& item = items_[i];
        const int y = ItemTop(i);

        double selectedAmount = 0.0;
        if (i == selected_) {
            selectedAmount = selectionBlend_;
        } else if (i == previousSelected_) {
            selectedAmount = 1.0 - selectionBlend_;
        }

        if (i == hovered_ && i != selected_ && hoverProgress_ > 0.0) {
            painter.FillRoundedRect(
                wxRect(cellX, PillTop(i), cellWidth, pillHeight), pillHeight / 2.0,
                WithAlpha(colors.onSurface,
                          static_cast<std::uint8_t>(12 * hoverProgress_)));
        }

        const Color idle = WithAlpha(colors.onSurface, 190);
        const Color foreground = Lerp(idle, colors.onSecondaryContainer, selectedAmount);

        int iconX = extended_ ? cellX + FromDIP(16) : cellX + (cellWidth - iconSize) / 2;
        const int iconY = y + (cellHeight - iconSize) / 2;
        IconProvider::Draw(painter, item.icon, wxRect(iconX, iconY, iconSize, iconSize),
                           foreground, 1.8);

        if (extended_ && labelAlpha_ > 0.01) {
            painter.DrawText(item.label,
                             wxRect(iconX + iconSize + FromDIP(12), y,
                                    cellWidth - (iconX - cellX) - iconSize - FromDIP(12),
                                    cellHeight),
                             Font(TypeStyle::Label),
                             WithAlpha(foreground,
                                       static_cast<std::uint8_t>(255 * labelAlpha_)),
                             wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        }

        if (item.badge > 0 && badgeProgress_ > 0.01) {
            const int badgeRadius = FromDIP(8);
            const double centerX = iconX + iconSize - FromDIP(2);
            const double centerY = iconY + FromDIP(2);
            const auto alpha = static_cast<std::uint8_t>(255 * badgeProgress_);
            painter.FillCircle(centerX, centerY, badgeRadius, WithAlpha(colors.error, alpha));
            painter.DrawText(
                std::to_string(item.badge),
                wxRect(static_cast<int>(centerX - badgeRadius),
                       static_cast<int>(centerY - badgeRadius), badgeRadius * 2,
                       badgeRadius * 2),
                MakeFont(TextStyle{10, 600, 0.0}, this), WithAlpha(colors.onError, alpha),
                wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
        }
    }
}

}  // namespace clashyuka::ui
