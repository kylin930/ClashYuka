#pragma once

#include <array>

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class ListItem;
class Switch;
class TextBlock;

class SettingsPage final : public PageBase {
public:
    SettingsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    Button* lightButton_ = nullptr;
    Button* darkButton_ = nullptr;
    std::array<Button*, 4> themeButtons_{};
    Switch* animations_ = nullptr;
    Switch* reducedMotion_ = nullptr;
    ListItem* kernelPathItem_ = nullptr;
    ListItem* apiItem_ = nullptr;
    ListItem* secretItem_ = nullptr;
};

}  // namespace clashyuka::ui
