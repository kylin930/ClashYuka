#pragma once

#include "clashyuka/ui/components/control_base.h"

namespace clashyuka::ui {

class Divider final : public ControlBase {
public:
    explicit Divider(wxWindow* parent, int insetDp = 0);

    void SetInset(int insetDp);

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    int insetDp_;
};

}  // namespace clashyuka::ui
