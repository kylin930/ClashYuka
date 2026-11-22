#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <wx/string.h>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/icons.h"
#include "clashyuka/ui/drawing/ripple.h"

namespace clashyuka::ui {

class ListItem final : public ControlBase {
public:
    explicit ListItem(wxWindow* parent);

    void SetTitle(const wxString& title);
    void SetSubtitle(const wxString& subtitle);
    void SetLeadingIcon(std::optional<IconId> icon);
    void SetTrailingText(const wxString& text);
    void SetShowChevron(bool show);
    void SetShowDivider(bool show);
    void SetSelected(bool selected, bool animate = true);
    bool IsSelected() const { return selected_; }
    void SetOnClick(std::function<void()> handler);

    wxSize DoGetBestClientSize() const override;
    bool Enable(bool enable = true) override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    void EmitClick();
    void UpdateHover(double target);
    void UpdatePress(double target);
    void UpdateSelected(double target);

    wxString title_;
    wxString subtitle_;
    wxString trailingText_;
    std::optional<IconId> leadingIcon_;
    bool showChevron_ = true;
    bool showDivider_ = false;
    bool selected_ = false;
    bool pressed_ = false;
    double hoverProgress_ = 0.0;
    double pressProgress_ = 0.0;
    double selectedProgress_ = 0.0;
    std::shared_ptr<AnimationHandle> hoverAnimation_;
    std::shared_ptr<AnimationHandle> pressAnimation_;
    std::shared_ptr<AnimationHandle> selectedAnimation_;
    Ripple ripple_;
    std::function<void()> onClick_;
};

}  // namespace clashyuka::ui
