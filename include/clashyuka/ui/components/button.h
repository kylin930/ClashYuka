#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <wx/string.h>

#include "clashyuka/ui/components/action_colors.h"
#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/icons.h"
#include "clashyuka/ui/drawing/ripple.h"

namespace clashyuka::ui {

class Button final : public ControlBase {
public:
    Button(wxWindow* parent, const wxString& label, ButtonVariant variant,
           std::optional<IconId> icon = std::nullopt);

    void SetLabel(const wxString& label);
    void SetVariant(ButtonVariant variant);
    void SetIcon(std::optional<IconId> icon);
    void SetOnClick(std::function<void()> handler);

    wxSize DoGetBestClientSize() const override;
    bool Enable(bool enable = true) override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    void UpdateHover(double target);
    void UpdatePress(double target);
    void UpdateFocus(double target);
    void UpdateDisabled(double target);
    void EmitClick();
    void BindInteraction();

    wxString label_;
    ButtonVariant variant_;
    std::optional<IconId> icon_;
    bool hovered_ = false;
    bool pressed_ = false;
    bool mouseInside_ = false;
    double hoverProgress_ = 0.0;
    double pressProgress_ = 0.0;
    double focusProgress_ = 0.0;
    double disabledProgress_ = 0.0;
    std::shared_ptr<AnimationHandle> hoverAnimation_;
    std::shared_ptr<AnimationHandle> pressAnimation_;
    std::shared_ptr<AnimationHandle> focusAnimation_;
    std::shared_ptr<AnimationHandle> disabledAnimation_;
    Ripple ripple_;
    std::function<void()> onClick_;
};

}  // namespace clashyuka::ui
