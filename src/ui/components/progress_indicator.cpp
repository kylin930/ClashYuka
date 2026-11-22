#include "clashyuka/ui/components/progress_indicator.h"

#include <algorithm>
#include <cmath>

#include "clashyuka/ui/drawing/motion_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

ProgressIndicator::ProgressIndicator(wxWindow* parent, ProgressKind kind)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize), kind_(kind) {
    SetMinSize(DoGetBestClientSize());
}

wxSize ProgressIndicator::DoGetBestClientSize() const {
    if (kind_ == ProgressKind::Linear) {
        return wxSize(FromDIP(220), FromDIP(4));
    }
    return wxSize(FromDIP(40), FromDIP(40));
}

void ProgressIndicator::SetValue(double value) {
    const double target = std::clamp(value, 0.0, 1.0);
    const bool completed = target >= 1.0 && value_ < 1.0;
    value_ = target;
    AnimateValue(target);
    if (completed) {
        completionPulse_ = 1.0;
        pulseAnimation_ = AnimationManager::Instance().Animate(
            1.0, 0.0, 360, motion::Soft, [this](double pulse) {
                completionPulse_ = pulse;
                Refresh(false);
            });
    }
    Refresh(false);
}

void ProgressIndicator::AnimateValue(double target) {
    valueAnimation_ = AnimationManager::Instance().Animate(
        displayedValue_, target, motion::ProgressMs, motion::Standard, [this](double value) {
            displayedValue_ = value;
            Refresh(false);
        });
}

void ProgressIndicator::SetIndeterminate(bool indeterminate) {
    if (indeterminate_ == indeterminate) {
        return;
    }
    indeterminate_ = indeterminate;
    if (!indeterminate_) {
        displayedValue_ = value_;
        AnimateValue(value_);
    }
    UpdateLoop();
    Refresh();
}

bool ProgressIndicator::Show(bool show) {
    const bool result = wxWindow::Show(show);
    UpdateLoop();
    return result;
}

void ProgressIndicator::UpdateLoop() {
    animation_.reset();
    if (!indeterminate_ || !IsShown()) {
        phase_ = 0.0;
        return;
    }
    animation_ = AnimationManager::Instance().AnimateLoop(
        0.0, 1.0, kind_ == ProgressKind::Linear ? 1200 : 1400, Easing::Linear,
        [this](double value) {
            phase_ = value;
            Refresh();
        });
}

void ProgressIndicator::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();

    if (kind_ == ProgressKind::Linear) {
        const double height = size.y;
        const double radius = height / 2.0;
        painter.FillRoundedRect(wxRect(0, 0, size.x, size.y), radius, colors.secondaryContainer);

        if (indeterminate_) {
            const double segment = std::max(size.x * 0.35, height * 3.0);
            const double x = -segment + (size.x + segment) * phase_;
            painter.SetClip(wxRect(0, 0, size.x, size.y));
            painter.FillRoundedRect(
                wxRect(static_cast<int>(x), 0, static_cast<int>(segment), size.y), radius,
                colors.primary);
            painter.ResetClip();
        } else if (displayedValue_ > 0.0) {
            const int width =
                std::max(static_cast<int>(size.x * displayedValue_), size.y);
            painter.FillRoundedRect(wxRect(0, 0, width, size.y), radius, colors.primary);
        }
        if (completionPulse_ > 0.01) {
            painter.FillRoundedRect(
                wxRect(0, 0, size.x, size.y), radius,
                WithAlpha(colors.primary,
                          static_cast<std::uint8_t>(45 * completionPulse_)));
        }
        return;
    }

    const double centerX = size.x / 2.0;
    const double centerY = size.y / 2.0;
    const double stroke = FromDIP(4);
    const double radius = std::max(1.0, std::min(size.x, size.y) / 2.0 - stroke / 2.0 - 1.0);

    painter.StrokeCircle(centerX, centerY, radius, colors.secondaryContainer, stroke);

    wxGraphicsPath path = painter.Gc()->CreatePath();
    if (indeterminate_) {
        const double start = -kPi / 2.0 + phase_ * 2.0 * kPi;
        path.AddArc(centerX, centerY, radius, start, start + kPi * 0.75, false);
    } else if (displayedValue_ > 0.0) {
        const double start = -kPi / 2.0;
        path.AddArc(centerX, centerY, radius, start, start + 2.0 * kPi * displayedValue_, false);
    }
    painter.StrokePath(path, colors.primary, stroke);
    if (completionPulse_ > 0.01) {
        painter.StrokeCircle(centerX, centerY, radius,
                             WithAlpha(colors.primary,
                                       static_cast<std::uint8_t>(70 * completionPulse_)),
                             stroke + FromDIP(2));
    }
}

}  // namespace clashyuka::ui
