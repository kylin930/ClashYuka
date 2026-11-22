#include "clashyuka/ui/components/dialog.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/region.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#if defined(__WXMSW__)
#include <wx/msw/wrapwin.h>
#endif

#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/icon_button.h"
#include "clashyuka/ui/drawing/motion_tokens.h"
#include "clashyuka/ui/drawing/painter.h"
#include "clashyuka/ui/theme/spacing_tokens.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {
namespace {

constexpr int kCornerRadiusDp = 28;

// Rounded window region: child windows cannot composite with what is behind
// them, so a shaped top-level window is the reliable way to get MD3 corners.
wxRegion RoundedWindowRegion(const wxSize& size, int radius) {
    if (size.x <= 0 || size.y <= 0) {
        return wxRegion();
    }
    const int limit = std::min(size.x, size.y) / 2;
    const int r = std::clamp(radius, 0, limit);
    if (r == 0) {
        return wxRegion(0, 0, size.x, size.y);
    }

    wxRegion region;
    for (int y = 0; y < size.y; ++y) {
        int inset = 0;
        double dy = 0.0;
        if (y < r) {
            dy = r - y - 0.5;
        } else if (y >= size.y - r) {
            dy = y - (size.y - r) + 0.5;
        } else {
            region.Union(0, y, size.x, 1);
            continue;
        }
        const double remaining = std::max(0.0, static_cast<double>(r) * r - dy * dy);
        inset = static_cast<int>(std::ceil(r - std::sqrt(remaining)));
        const int width = size.x - inset * 2;
        if (width > 0) {
            region.Union(inset, y, width, 1);
        }
    }
    return region;
}

}  // namespace

Dialog::Dialog(wxWindow* parent, const wxString& title, const wxString& body)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
    const ColorScheme& colors = ThemeManager::Instance().Colors();
    SetBackgroundColour(ToWx(colors.surface));
    SetForegroundColour(ToWx(colors.onSurface));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &Dialog::OnPaint, this);

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxBoxSizer(wxHORIZONTAL);
    auto* titleLabel = new wxStaticText(this, wxID_ANY, title);
    titleLabel->SetFont(MakeFont(ThemeManager::Instance().Type().Get(TypeStyle::Title), this));
    titleLabel->SetForegroundColour(ToWx(colors.onSurface));
    header->Add(titleLabel, 1, wxALIGN_CENTER_VERTICAL);
    auto* closeButton = new IconButton(this, IconId::Close, IconButtonVariant::Standard);
    closeButton->SetOnClick([this] { EndModal(wxID_CANCEL); });
    header->Add(closeButton, 0, wxLEFT, FromDIP(SpaceDp(Space::Sm)));
    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(SpaceDp(Space::Xxl)));

    // Borderless windows need an explicit drag area.
    titleLabel->Bind(wxEVT_LEFT_DOWN, &Dialog::OnDragStart, this);

    auto* bodyLabel = new wxStaticText(this, wxID_ANY, body);
    bodyLabel->SetFont(MakeFont(ThemeManager::Instance().Type().Get(TypeStyle::Body), this));
    bodyLabel->SetForegroundColour(ToWx(WithAlpha(colors.onSurface, 190)));
    bodyLabel->Wrap(FromDIP(360));
    root->Add(bodyLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(SpaceDp(Space::Lg)));

    actionRow_ = new wxBoxSizer(wxHORIZONTAL);
    root->Add(actionRow_, 0, wxALIGN_RIGHT | wxALL, FromDIP(SpaceDp(Space::Xxl)));

    SetSizerAndFit(root);
    CentreOnParent();
    UpdateWindowShape();
    Bind(wxEVT_CLOSE_WINDOW, &Dialog::OnClose, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        // Only the header behaves like a caption; body clicks stay inert.
        if (event.GetPosition().y < FromDIP(72)) {
            OnDragStart(event);
        } else {
            event.Skip();
        }
    });
    Bind(wxEVT_MOTION, &Dialog::OnDragMove, this);
    Bind(wxEVT_LEFT_UP, &Dialog::OnDragEnd, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { dragging_ = false; });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        ThemeManager::Instance().SetKeyboardNavigation(true);
        if (event.GetKeyCode() == WXK_ESCAPE) {
            EndModal(wxID_CANCEL);
            return;
        }
        event.Skip();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        ThemeManager::Instance().SetKeyboardNavigation(true);
        if (event.GetKeyCode() == WXK_ESCAPE) {
            EndModal(wxID_CANCEL);
            return;
        }
        event.Skip();
    });
}

int Dialog::ShowModal() {
    // The dialog manager focuses the first control when the window opens; that
    // is not keyboard navigation and must not leave a focus state layer.
    ThemeManager::Instance().SetKeyboardNavigation(false);
    // Shaped window + layered alpha: fade/slide in once the layout is final.
    if (ThemeManager::Instance().MotionEnabled() && !exiting_) {
        targetY_ = GetPosition().y;
        SetPosition(wxPoint(GetPosition().x, targetY_ + FromDIP(8)));
        opacity_ = 0.0;
        ApplyWindowAlpha(0.0);
        animation_ = AnimationManager::Instance().Animate(
            0.0, 1.0, motion::DialogMs, motion::Enter, [this](double value) {
                opacity_ = value;
                SetPosition(wxPoint(GetPosition().x,
                                    targetY_ + static_cast<int>(FromDIP(8) * (1.0 - value))));
                ApplyWindowAlpha(value);
            });
    } else {
        opacity_ = 1.0;
    }
    return wxDialog::ShowModal();
}

void Dialog::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const ColorScheme& colors = ThemeManager::Instance().Colors();
    dc.SetBackground(wxBrush(ToWx(colors.surface)));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(ToWx(WithAlpha(colors.outlineVariant, 150)), 1.0));
    gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, FromDIP(kCornerRadiusDp));
}

void Dialog::OnDragStart(wxMouseEvent& event) {
    if (!event.LeftDown()) {
        return;
    }
    dragging_ = true;
    dragStartScreen_ = wxGetMousePosition();
    dragStartPosition_ = GetPosition();
    if (!HasCapture()) {
        CaptureMouse();
    }
}

void Dialog::OnDragMove(wxMouseEvent& event) {
    if (dragging_ && event.LeftIsDown()) {
        const wxPoint delta = wxGetMousePosition() - dragStartScreen_;
        Move(dragStartPosition_ + delta);
        return;
    }
    event.Skip();
}

void Dialog::OnDragEnd(wxMouseEvent& event) {
    if (dragging_) {
        dragging_ = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
    event.Skip();
}

void Dialog::UpdateWindowShape() {
#if wxUSE_SHAPE
    SetShape(RoundedWindowRegion(GetClientSize(), FromDIP(kCornerRadiusDp)));
#endif
}

void Dialog::EndModal(int retCode) {
    if (exiting_) {
        // Already fading out; ignore repeated EndModal requests.
        return;
    }
    if (IsModal() && ThemeManager::Instance().MotionEnabled()) {
        StartExit(retCode);
        return;
    }
    wxDialog::EndModal(retCode);
}

void Dialog::OnClose(wxCloseEvent& event) {
    if (exiting_) {
        event.Veto();
        return;
    }
    if (ThemeManager::Instance().MotionEnabled() && IsModal()) {
        StartExit(wxID_CANCEL);
        event.Veto();
        return;
    }
    if (IsModal()) {
        wxDialog::EndModal(wxID_CANCEL);
    } else {
        event.Skip();
    }
}

void Dialog::StartExit(int retCode) {
    exiting_ = true;
    exitCode_ = retCode;
    animation_.reset();

    const wxPoint start = GetPosition();
    const double startOpacity = opacity_;
    const int travel = FromDIP(8);
    animation_ = AnimationManager::Instance().Animate(
        0.0, 1.0, motion::SurfaceOutMs, motion::Exit,
        [this, start, startOpacity, travel](double value) {
            SetPosition(wxPoint(start.x, start.y + static_cast<int>(travel * value)));
            opacity_ = startOpacity * (1.0 - value);
            ApplyWindowAlpha(opacity_);
            if (value >= 1.0 && !exitScheduled_) {
                exitScheduled_ = true;
                // Leave the timer callback before ending the modal loop.
                CallAfter([this] { FinishExit(); });
            }
        });
}

void Dialog::FinishExit() {
    animation_.reset();
    if (IsModal()) {
        wxDialog::EndModal(exitCode_);
    } else {
        Destroy();
    }
}

void Dialog::ApplyWindowAlpha(double opacity) {
#if defined(__WXMSW__)
    HWND hwnd = static_cast<HWND>(GetHandle());
    if (hwnd == nullptr) {
        return;
    }
    const LONG_PTR extended = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if ((extended & WS_EX_LAYERED) == 0) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, extended | WS_EX_LAYERED);
    }
    const auto alpha =
        static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0, 1.0) * 255.0));
    ::SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
#else
    // Other platforms: keep the native window fully opaque (content-only motion).
    (void)opacity;
#endif
}

void Dialog::AddAction(const wxString& label, ButtonVariant variant, int modalResult,
                       std::function<void()> onClick) {
    auto* button = new Button(this, label, variant);
    button->SetSurfaceColor(ThemeManager::Instance().Colors().surface);
    button->SetOnClick([this, modalResult, handler = std::move(onClick)] {
        if (handler) {
            handler();
        }
        if (modalResult != wxID_NONE) {
            EndModal(modalResult);
        }
    });
    actionRow_->Add(button, 0, wxLEFT, FromDIP(SpaceDp(Space::Sm)));
    actionRow_->Layout();
    Layout();
    Fit();
    CentreOnParent();
    UpdateWindowShape();
}

bool Dialog::Confirm(wxWindow* parent, const wxString& title, const wxString& body,
                     const wxString& confirmLabel, const wxString& cancelLabel) {
    Dialog dialog(parent, title, body);
    dialog.AddAction(cancelLabel, ButtonVariant::Text, wxID_CANCEL);
    dialog.AddAction(confirmLabel, ButtonVariant::Filled, wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}

}  // namespace clashyuka::ui
