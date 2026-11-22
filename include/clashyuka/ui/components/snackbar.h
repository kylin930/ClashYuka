#pragma once

#include <functional>
#include <memory>

#include <wx/timer.h>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

class Snackbar final : public ControlBase {
public:
    static Snackbar* Present(wxWindow* parent, const wxString& message,
                             const wxString& actionLabel = {},
                             std::function<void()> onAction = {}, int durationMs = 3500);

    ~Snackbar() override;

    void Dismiss();
    // Reuses the visible snackbar instead of stacking a second one.
    void ReplaceMessage(const wxString& message, const wxString& actionLabel,
                        std::function<void()> onAction, int durationMs);

    void OnThemeChanged() override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    Snackbar(wxWindow* parent, wxString message, wxString actionLabel,
             std::function<void()> onAction, int durationMs);

    void UpdateLayout();
    void Reposition();
    void AnimateIn();
    void OnParentSize(wxSizeEvent& event);
    void OnTimer(wxTimerEvent& event);

    wxWindow* parentWindow_;
    wxString message_;
    wxString actionLabel_;
    std::function<void()> onAction_;
    int durationMs_;
    bool closing_ = false;
    double offsetY_ = 0.0;
    double opacity_ = 0.0;
    wxTimer timer_;
    std::shared_ptr<AnimationHandle> animation_;
    std::shared_ptr<AnimationHandle> fadeAnimation_;
    wxRect actionRect_;
    wxRect closeRect_;
};

}  // namespace clashyuka::ui
