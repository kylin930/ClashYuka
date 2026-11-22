#include "clashyuka/ui/components/tab_bar.h"

#include <algorithm>
#include <utility>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kHeightDp = 48;

}  // namespace

TabBar::TabBar(wxWindow* parent)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS) {
    SetMinSize(DoGetBestClientSize());

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        RebuildLayout();
        scrollOffset_ = std::clamp(scrollOffset_, 0, MaxScroll());
        UpdateIndicator(false);
        event.Skip();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const int index = HitTest(event.GetX() + scrollOffset_);
        if (index >= 0 && index != selected_) {
            selected_ = index;
            UpdateIndicator(true);
            Refresh();
            if (onSelect_) {
                onSelect_(index);
            }
            wxCommandEvent command(wxEVT_BUTTON, GetId());
            command.SetEventObject(this);
            command.SetInt(index);
            ProcessWindowEvent(command);
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const int index = HitTest(event.GetX() + scrollOffset_);
        if (index != hovered_) {
            hovered_ = index;
            Refresh();
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hovered_ = -1;
        Refresh();
    });
    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) {
        const int maxScroll = MaxScroll();
        if (maxScroll <= 0) {
            return;
        }
        scrollOffset_ = std::clamp(scrollOffset_ - event.GetWheelRotation() / 2, 0, maxScroll);
        Refresh();
    });
}

wxSize TabBar::DoGetBestClientSize() const {
    return wxSize(FromDIP(320), FromDIP(kHeightDp));
}

void TabBar::SetTabs(std::vector<std::string> tabs) {
    layout_.clear();
    for (auto& label : tabs) {
        layout_.push_back(TabLayout{std::move(label), 0, 0});
    }
    selected_ = layout_.empty() ? 0 : std::clamp(selected_, 0, static_cast<int>(layout_.size()) - 1);
    previousSelected_ = selected_;
    selectionBlend_ = 1.0;
    RebuildLayout();
    scrollOffset_ = 0;
    UpdateIndicator(false);
    SetMinSize(DoGetBestClientSize());
    InvalidateBestSize();
    Refresh();
}

void TabBar::SetSelected(int index) {
    if (index < 0 || index >= static_cast<int>(layout_.size()) || index == selected_) {
        return;
    }
    previousSelected_ = selected_;
    selected_ = index;
    UpdateIndicator(true);

    selectionBlend_ = 0.0;
    colorAnimation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::TabMs, motion::Standard, [this](double value) {
            selectionBlend_ = value;
            Refresh(false);
        });
    Refresh(false);
}

void TabBar::SetOnSelect(std::function<void(int)> handler) {
    onSelect_ = std::move(handler);
}

void TabBar::RebuildLayout() {
    const wxFont font = Font(TypeStyle::Label);
    int x = 0;
    for (auto& tab : layout_) {
        int textWidth = 0;
        int textHeight = 0;
        GetTextExtent(wxString::FromUTF8(tab.label), &textWidth, &textHeight, nullptr, nullptr,
                      &font);
        tab.width = std::max(FromDIP(64), textWidth + FromDIP(32));
        tab.x = x;
        x += tab.width;
    }
}

int TabBar::MaxScroll() const {
    if (layout_.empty()) {
        return 0;
    }
    const int total = layout_.back().x + layout_.back().width;
    return std::max(0, total - GetClientSize().x);
}

void TabBar::UpdateIndicator(bool animate) {
    if (layout_.empty() || selected_ < 0 || selected_ >= static_cast<int>(layout_.size())) {
        return;
    }
    const TabLayout& tab = layout_[selected_];
    const double targetX = tab.x + FromDIP(8);
    const double targetWidth = std::max(0, tab.width - FromDIP(16));

    if (!animate) {
        indicatorX_ = targetX;
        indicatorWidth_ = targetWidth;
        Refresh();
        return;
    }

    xAnimation_ = AnimationManager::Instance().Animate(
        indicatorX_, targetX, motion::TabMs, motion::Standard, [this](double value) {
            indicatorX_ = value;
            Refresh(false);
        });
    widthAnimation_ = AnimationManager::Instance().Animate(
        indicatorWidth_, targetWidth, motion::TabMs, motion::Standard, [this](double value) {
            indicatorWidth_ = value;
            Refresh(false);
        });
}

int TabBar::HitTest(int x) const {
    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        if (x >= layout_[i].x && x < layout_[i].x + layout_[i].width) {
            return i;
        }
    }
    return -1;
}

void TabBar::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    painter.SetClip(wxRect(0, 0, size.x, size.y));

    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        const TabLayout& tab = layout_[i];
        const int x = tab.x - scrollOffset_;
        if (x + tab.width < 0 || x > size.x) {
            continue;
        }
        if (i == hovered_ && i != selected_) {
            painter.FillRoundedRect(wxRect(x + FromDIP(4), FromDIP(6), tab.width - FromDIP(8),
                                           size.y - FromDIP(12)),
                                    FromDIP(8), WithAlpha(colors.onSurface, 10));
        }
        painter.DrawText(tab.label, wxRect(x, 0, tab.width, size.y), Font(TypeStyle::Label),
                         Lerp(WithAlpha(colors.onSurface, 170), colors.primary,
                              i == selected_ ? selectionBlend_
                                             : (i == previousSelected_ ? 1.0 - selectionBlend_
                                                                       : 0.0)),
                         wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
    }

    const double indicatorY = size.y - FromDIP(3);
    painter.FillRoundedRect(wxRect(static_cast<int>(indicatorX_ - scrollOffset_),
                                   static_cast<int>(indicatorY),
                                   static_cast<int>(std::max(0.0, indicatorWidth_)),
                                   FromDIP(3)),
                            FromDIP(1.5), colors.primary);
    painter.ResetClip();
}

}  // namespace clashyuka::ui
