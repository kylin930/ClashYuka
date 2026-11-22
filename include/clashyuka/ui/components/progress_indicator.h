#pragma once

#include <memory>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

enum class ProgressKind { Linear, Circular };

class ProgressIndicator final : public ControlBase {
public:
    ProgressIndicator(wxWindow* parent, ProgressKind kind);

    void SetValue(double value);
    double GetValue() const { return value_; }

    void SetIndeterminate(bool indeterminate);
    bool IsIndeterminate() const { return indeterminate_; }

    wxSize DoGetBestClientSize() const override;
    bool Show(bool show = true) override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    void UpdateLoop();
    void AnimateValue(double target);

    ProgressKind kind_;
    double value_ = 0.0;
    double displayedValue_ = 0.0;
    double completionPulse_ = 0.0;
    bool indeterminate_ = false;
    double phase_ = 0.0;
    std::shared_ptr<AnimationHandle> animation_;
    std::shared_ptr<AnimationHandle> valueAnimation_;
    std::shared_ptr<AnimationHandle> pulseAnimation_;
};

}  // namespace clashyuka::ui
