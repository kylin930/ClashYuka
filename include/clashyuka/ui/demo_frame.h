#pragma once

#include <memory>
#include <vector>

#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>

#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {

class Button;
class TabBar;
class TextBlock;
class NavigationRail;
class Switch;
class ProgressIndicator;

class DemoFrame final : public wxFrame, public ThemeObserver {
public:
    DemoFrame();
    ~DemoFrame() override;

    void OnThemeChanged() override;

private:
    void BuildUi();
    wxPanel* BuildOverviewPage(wxWindow* parent);
    wxPanel* BuildComponentsPage(wxWindow* parent);
    wxPanel* BuildFeedbackPage(wxWindow* parent);
    wxPanel* BuildMotionPage(wxWindow* parent);
    wxPanel* BuildAboutPage(wxWindow* parent);
    void SelectPage(int index);
    void UpdateThemeControls();
    void ApplyThemeToChrome();

    wxPanel* root_ = nullptr;
    wxPanel* header_ = nullptr;
    wxScrolledWindow* content_ = nullptr;
    wxBoxSizer* contentSizer_ = nullptr;
    NavigationRail* rail_ = nullptr;
    TabBar* tabBar_ = nullptr;
    wxSimplebook* tabBook_ = nullptr;
    std::vector<wxPanel*> pages_;
    std::vector<Button*> themeButtons_;
    Button* darkButton_ = nullptr;
    Switch* motionSwitch_ = nullptr;
    Switch* reducedSwitch_ = nullptr;
    TextBlock* pageTitle_ = nullptr;
    TextBlock* pageSubtitle_ = nullptr;
    ProgressIndicator* motionProgress_ = nullptr;
    double motionValue_ = 0.3;
    int currentPage_ = 0;
    std::shared_ptr<AnimationHandle> pageAnimation_;
    std::shared_ptr<AnimationHandle> tabPageAnimation_;
};

}  // namespace clashyuka::ui
