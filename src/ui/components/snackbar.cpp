#include "clashyuka/ui/components/snackbar.h"

#include <algorithm>
#include <utility>

#include "clashyuka/ui/drawing/icons.h"
#include "clashyuka/ui/drawing/motion_tokens.h"
#include "clashyuka/ui/theme/shape_tokens.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

constexpr int kHeightDp = 48;
constexpr int kCloseSizeDp = 16;
constexpr const char* kSnackbarName = "clashyuka-snackbar";

}  // namespace

Snackbar* Snackbar::Present(wxWindow* parent, const wxString& message,
                            const wxString& actionLabel, std::function<void()> onAction,
                            int durationMs) {
    if (parent == nullptr) {
        return nullptr;
    }
    if (auto* existing =
            dynamic_cast<Snackbar*>(parent->FindWindowByName(kSnackbarName, parent))) {
        existing->ReplaceMessage(message, actionLabel, std::move(onAction), durationMs);
        existing->Show(true);
        existing->Raise();
        existing->AnimateIn();
        return existing;
    }

    auto* snackbar = new Snackbar(parent, message, actionLabel, std::move(onAction), durationMs);
    snackbar->Show(true);
    snackbar->Raise();
    snackbar->AnimateIn();
    return snackbar;
}

Snackbar::Snackbar(wxWindow* parent, wxString message, wxString actionLabel,
                   std::function<void()> onAction, int durationMs)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
      parentWindow_(parent),
      message_(std::move(message)),
      actionLabel_(std::move(actionLabel)),
      onAction_(std::move(onAction)),
      durationMs_(std::max(500, durationMs)) {
    SetName(kSnackbarName);
    SetOpaqueBackground(false);
    SetSurfaceColor(Colors().inverseSurface);
    SetCursor(wxCursor(wxCURSOR_HAND));

    UpdateLayout();
    Reposition();

    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, &Snackbar::OnTimer, this);
    timer_.StartOnce(durationMs_);

    parentWindow_->Bind(wxEVT_SIZE, &Snackbar::OnParentSize, this);

    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const wxPoint position = event.GetPosition();
        if (closeRect_.Contains(position)) {
            Dismiss();
            return;
        }
        if (!actionLabel_.empty() && actionRect_.Contains(position)) {
            if (onAction_) {
                onAction_();
            }
            Dismiss();
        }
    });
}

Snackbar::~Snackbar() {
    timer_.Stop();
    if (parentWindow_ != nullptr) {
        parentWindow_->Unbind(wxEVT_SIZE, &Snackbar::OnParentSize, this);
    }
}

void Snackbar::OnThemeChanged() {
    SetSurfaceColor(Colors().inverseSurface);
}

void Snackbar::UpdateLayout() {
    const wxFont font = Font(TypeStyle::Label);
    int textWidth = 0;
    int textHeight = 0;
    GetTextExtent(message_, &textWidth, &textHeight, nullptr, nullptr, &font);
    int actionWidth = 0;
    if (!actionLabel_.empty()) {
        GetTextExtent(actionLabel_, &actionWidth, &textHeight, nullptr, nullptr, &font);
    }

    const int padding = FromDIP(SpaceDp(Space::Lg));
    int width = padding * 2 + textWidth + FromDIP(kCloseSizeDp) + FromDIP(12);
    if (actionWidth > 0) {
        width += actionWidth + FromDIP(20);
    }
    const int maxWidth =
        std::max(FromDIP(200), parentWindow_->GetClientSize().x - FromDIP(32));
    width = std::min(width, maxWidth);
    SetSize(wxSize(width, FromDIP(kHeightDp)));
}

void Snackbar::ReplaceMessage(const wxString& message, const wxString& actionLabel,
                              std::function<void()> onAction, int durationMs) {
    closing_ = false;
    message_ = message;
    actionLabel_ = actionLabel;
    onAction_ = std::move(onAction);
    durationMs_ = std::max(500, durationMs);
    UpdateLayout();
    Reposition();
    timer_.Stop();
    timer_.StartOnce(durationMs_);
    Refresh();
}

void Snackbar::SetBottomInset(int insetDp) {
    bottomInsetDp_ = std::max(0, insetDp);
    Reposition();
    Refresh();
}

void Snackbar::Reposition() {
    if (parentWindow_ == nullptr) {
        return;
    }
    const wxSize parentSize = parentWindow_->GetClientSize();
    const wxSize size = GetSize();
    const int x = (parentSize.x - size.x) / 2;
    const int y = parentSize.y - size.y - FromDIP(bottomInsetDp_) +
                  static_cast<int>(offsetY_);
    SetPosition(wxPoint(std::max(0, x), std::max(0, y)));
}

void Snackbar::AnimateIn() {
    offsetY_ = FromDIP(SpaceDp(Space::Md));
    opacity_ = 0.0;
    Reposition();
    animation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::SnackbarMs, motion::Enter, [this](double value) {
            opacity_ = value;
            offsetY_ = FromDIP(SpaceDp(Space::Md)) * (1.0 - value);
            Reposition();
            Refresh(false);
        });
}

void Snackbar::Dismiss() {
    if (closing_) {
        return;
    }
    closing_ = true;
    timer_.Stop();
    animation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::SurfaceOutMs, motion::Exit, [this](double value) {
            opacity_ = 1.0 - value;
            offsetY_ = FromDIP(SpaceDp(Space::Md)) * value;
            Reposition();
            Refresh(false);
        });
    timer_.StartOnce(motion::SurfaceOutMs + 20);
}

void Snackbar::OnParentSize(wxSizeEvent& event) {
    Reposition();
    event.Skip();
}

void Snackbar::OnTimer(wxTimerEvent&) {
    if (closing_) {
        Destroy();
        return;
    }
    Dismiss();
}

void Snackbar::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const auto opacity = static_cast<std::uint8_t>(std::lround(255.0 * std::clamp(opacity_, 0.0, 1.0)));
    if (opacity == 0) {
        return;
    }

    painter.FillRoundedRect(wxRect(0, 0, size.x, size.y), FromDIP(ShapeDp(ShapeToken::Small)),
                            WithAlpha(colors.inverseSurface, opacity));

    const int padding = FromDIP(SpaceDp(Space::Lg));
    const int closeSize = FromDIP(kCloseSizeDp);
    const int iconY = (size.y - closeSize) / 2;
    closeRect_ = wxRect(size.x - padding - closeSize, iconY, closeSize, closeSize);

    const wxFont font = Font(TypeStyle::Label);
    int actionWidth = 0;
    if (!actionLabel_.empty()) {
        actionWidth = painter.MeasureText(actionLabel_.utf8_string(), font).x;
        actionRect_ = wxRect(closeRect_.x - FromDIP(12) - actionWidth, 0, actionWidth, size.y);
        painter.DrawText(actionLabel_.utf8_string(), actionRect_, font,
                         WithAlpha(colors.inversePrimary, opacity),
                         wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    } else {
        actionRect_ = wxRect();
    }

    const int messageRight = (actionWidth > 0 ? actionRect_.x : closeRect_.x) - FromDIP(12);
    painter.DrawText(message_.utf8_string(), wxRect(padding, 0, messageRight - padding, size.y),
                     font, WithAlpha(colors.inverseOnSurface, opacity),
                     wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    IconProvider::Draw(painter, IconId::Close, closeRect_,
                       WithAlpha(colors.inverseOnSurface, static_cast<std::uint8_t>(opacity * 0.7)),
                       1.8);
}

}  // namespace clashyuka::ui
