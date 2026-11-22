#pragma once

#include <array>

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class ListItem;
class SearchField;
class Switch;
class TextBlock;

class SettingsPage final : public PageBase {
public:
    SettingsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    void BrowseKernelPath();
    void SaveMihomoSettings();
    void SyncMihomoSettings();

    Button* lightButton_ = nullptr;
    Button* darkButton_ = nullptr;
    std::array<Button*, 4> themeButtons_{};
    Switch* animations_ = nullptr;
    Switch* reducedMotion_ = nullptr;
    SearchField* kernelPathField_ = nullptr;
    SearchField* apiPortField_ = nullptr;
    SearchField* secretField_ = nullptr;
    Switch* autoStart_ = nullptr;
    TextBlock* kernelStatusLine_ = nullptr;
    bool mihomoFieldsInitialized_ = false;
};

}  // namespace clashyuka::ui
