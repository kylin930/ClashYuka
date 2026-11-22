#pragma once

#include <functional>
#include <memory>

#include <wx/panel.h>

#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {

enum class SurfaceLevel { Lowest, Low, Container, High, Highest };

// Rounded MD3 surface that can host arbitrary children via SetContent().
class SurfaceCard : public wxPanel, public ThemeObserver {
public:
    explicit SurfaceCard(wxWindow* parent, SurfaceLevel level = SurfaceLevel::Container);
    ~SurfaceCard() override;

    void SetLevel(SurfaceLevel level);
    void SetOutlined(bool outlined);
    void SetClickable(bool clickable);
    void SetPadding(int paddingDp) { paddingDp_ = paddingDp; }
    void SetContent(wxSizer* content, int paddingDp = -1);
    void SetOnClick(std::function<void()> handler);

    void OnThemeChanged() override;

private:
    Color CardColor() const;
    void OnPaint(wxPaintEvent& event);
    void UpdateHover(double target);
    void UpdatePress(double target);
    void EmitClick();

    SurfaceLevel level_;
    bool outlined_ = false;
    bool clickable_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    int paddingDp_ = 16;
    double hoverProgress_ = 0.0;
    double pressProgress_ = 0.0;
    std::shared_ptr<AnimationHandle> hoverAnimation_;
    std::shared_ptr<AnimationHandle> pressAnimation_;
    std::function<void()> onClick_;
};

}  // namespace clashyuka::ui
