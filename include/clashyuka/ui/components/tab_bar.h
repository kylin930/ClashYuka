#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui {

class TabBar final : public ControlBase {
public:
    explicit TabBar(wxWindow* parent);

    void SetTabs(std::vector<std::string> tabs);
    void SetSelected(int index);
    int GetSelected() const { return selected_; }
    void SetOnSelect(std::function<void(int)> handler);

    wxSize DoGetBestClientSize() const override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    struct TabLayout {
        std::string label;
        int x = 0;
        int width = 0;
    };

    void RebuildLayout();
    void UpdateIndicator(bool animate);
    int HitTest(int x) const;
    int MaxScroll() const;

    std::vector<TabLayout> layout_;
    int selected_ = 0;
    int previousSelected_ = 0;
    int hovered_ = -1;
    int scrollOffset_ = 0;
    double indicatorX_ = 0.0;
    double indicatorWidth_ = 0.0;
    double selectionBlend_ = 1.0;
    std::shared_ptr<AnimationHandle> xAnimation_;
    std::shared_ptr<AnimationHandle> widthAnimation_;
    std::shared_ptr<AnimationHandle> colorAnimation_;
    std::function<void(int)> onSelect_;
};

}  // namespace clashyuka::ui
