#pragma once

#include <functional>
#include <memory>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/icons.h"
#include "clashyuka/ui/drawing/ripple.h"

namespace clashyuka::ui {

enum class IconButtonVariant { Standard, Filled, FilledTonal, Outlined };

class IconButton final : public ControlBase {
public:
    IconButton(wxWindow* parent, IconId icon, IconButtonVariant variant);

    void SetIcon(IconId icon);
    void SetVariant(IconButtonVariant variant);
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

    IconId icon_;
    IconButtonVariant variant_;
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
