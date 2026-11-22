#include "clashyuka/ui/drawing/ripple.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {

Ripple::~Ripple() {
    Cancel();
}

void Ripple::Trigger(const wxPoint& origin, const wxSize& size, std::function<void()> onUpdate) {
    Cancel();
    if (size.x <= 0 || size.y <= 0) {
        return;
    }
    originX_ = origin.x;
    originY_ = origin.y;
    progress_ = 0.001;
    onUpdate_ = std::move(onUpdate);
    animation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::RippleMs, Easing::EaseOutCubic, [this](double value) {
            progress_ = value;
            if (onUpdate_) {
                onUpdate_();
            }
        });
    if (onUpdate_) {
        onUpdate_();
    }
}

void Ripple::Cancel() {
    animation_.reset();
    progress_ = 0.0;
}

void Ripple::Paint(Painter& painter, const wxSize& size, double cornerRadius,
                   const Color& color) {
    if (!IsActive()) {
        return;
    }
    const double maxRadius = std::sqrt(static_cast<double>(size.x) * size.x +
                                       static_cast<double>(size.y) * size.y);
    const double radius = maxRadius * 0.75 * progress_;
    const auto alpha = static_cast<std::uint8_t>(
        std::lround(255.0 * 0.16 * (1.0 - progress_)));
    if (alpha == 0) {
        return;
    }

    painter.SetClipRoundedRect(wxRect(0, 0, size.x, size.y), cornerRadius);
    painter.FillCircle(originX_, originY_, radius, WithAlpha(color, alpha));
    painter.ResetClip();
}

}  // namespace clashyuka::ui
