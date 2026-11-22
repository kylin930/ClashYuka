#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/icons.h"

namespace clashyuka::ui {

struct NavigationRailItem {
    IconId icon;
    std::string label;
    int badge = 0;
};

class NavigationRail final : public ControlBase {
public:
    NavigationRail(wxWindow* parent, bool extended);

    void SetItems(std::vector<NavigationRailItem> items);
    void SetSelected(int index);
    int GetSelected() const { return selected_; }
    void SetExtended(bool extended, bool animate = true);
    void SetOnSelect(std::function<void(int)> handler);

    wxSize DoGetBestClientSize() const override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    int HitTest(const wxPoint& point) const;
    int ItemHeightPx() const;
    int PillHeightPx() const;
    int ItemTop(int index) const;
    int PillTop(int index) const;
    void SnapIndicator();
    void UpdateHover(double target);

    std::vector<NavigationRailItem> items_;
    int selected_ = -1;
    int previousSelected_ = -1;
    int hovered_ = -1;
    bool extended_;
    double indicatorY_ = 0.0;
    double selectionBlend_ = 1.0;
    double hoverProgress_ = 0.0;
    double badgeProgress_ = 1.0;
    double labelAlpha_ = 1.0;
    std::shared_ptr<AnimationHandle> indicatorAnimation_;
    std::shared_ptr<AnimationHandle> blendAnimation_;
    std::shared_ptr<AnimationHandle> hoverAnimation_;
    std::shared_ptr<AnimationHandle> badgeAnimation_;
    std::shared_ptr<AnimationHandle> labelAnimation_;
    std::function<void(int)> onSelect_;
};

}  // namespace clashyuka::ui
