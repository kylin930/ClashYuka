#pragma once

#include <functional>
#include <memory>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

class Switch final : public ControlBase {
public:
    explicit Switch(wxWindow* parent);

    bool GetValue() const { return value_; }
    void SetValue(bool value, bool animate = true);
    void SetOnChange(std::function<void(bool)> handler);

    wxSize DoGetBestClientSize() const override;
    bool Enable(bool enable = true) override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    void Toggle();
    void AnimateTo(double target);
    void UpdateHover(double target);
    void UpdatePress(double target);
    void UpdateDisabled(double target);

    bool value_ = false;
    bool pressed_ = false;
    bool mouseInside_ = false;
    double progress_ = 0.0;
    double hoverProgress_ = 0.0;
    double pressProgress_ = 0.0;
    double disabledProgress_ = 0.0;
    std::shared_ptr<AnimationHandle> animation_;
    std::shared_ptr<AnimationHandle> hoverAnimation_;
    std::shared_ptr<AnimationHandle> pressAnimation_;
    std::shared_ptr<AnimationHandle> disabledAnimation_;
    std::function<void(bool)> onChange_;
};

}  // namespace clashyuka::ui
