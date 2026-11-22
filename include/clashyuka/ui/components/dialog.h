#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <wx/dialog.h>

#include "clashyuka/ui/components/action_colors.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

class Dialog final : public wxDialog {
public:
    Dialog(wxWindow* parent, const wxString& title, const wxString& body);

    // modalResult is passed to EndModal when the action is clicked.
    void AddAction(const wxString& label, ButtonVariant variant, int modalResult,
                   std::function<void()> onClick = {});

    static bool Confirm(wxWindow* parent, const wxString& title, const wxString& body,
                        const wxString& confirmLabel = "Confirm",
                        const wxString& cancelLabel = "Cancel");

    // Runs the reverse fade/slide animation before the modal loop ends.
    void EndModal(int retCode) override;

    // Starts the enter animation once the final layout is known.
    int ShowModal() override;

private:
    void OnClose(wxCloseEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnDragStart(wxMouseEvent& event);
    void OnDragMove(wxMouseEvent& event);
    void OnDragEnd(wxMouseEvent& event);
    void StartExit(int retCode);
    void FinishExit();
    void ApplyWindowAlpha(double opacity);
    void UpdateWindowShape();

    wxBoxSizer* actionRow_ = nullptr;
    std::shared_ptr<AnimationHandle> animation_;
    int targetY_ = 0;
    double opacity_ = 1.0;
    int exitCode_ = wxID_CANCEL;
    bool exiting_ = false;
    bool exitScheduled_ = false;
    bool dragging_ = false;
    wxPoint dragStartScreen_;
    wxPoint dragStartPosition_;
};

}  // namespace clashyuka::ui
