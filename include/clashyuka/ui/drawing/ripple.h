#pragma once

#include <functional>
#include <memory>

#include <wx/gdicmn.h>

#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/painter.h"

namespace clashyuka::ui {

// Click ripple driven by the shared AnimationManager. Origin is the actual
// press position, never the control centre (keyboard input uses the centre).
class Ripple {
public:
    Ripple() = default;
    ~Ripple();

    Ripple(const Ripple&) = delete;
    Ripple& operator=(const Ripple&) = delete;

    void Trigger(const wxPoint& origin, const wxSize& size, std::function<void()> onUpdate);
    void Cancel();
    bool IsActive() const { return progress_ > 0.0 && progress_ < 1.0; }

    void Paint(Painter& painter, const wxSize& size, double cornerRadius, const Color& color);

private:
    double originX_ = 0.0;
    double originY_ = 0.0;
    double progress_ = 0.0;
    std::shared_ptr<AnimationHandle> animation_;
    std::function<void()> onUpdate_;
};

}  // namespace clashyuka::ui
