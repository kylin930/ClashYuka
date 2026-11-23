#pragma once

#include <array>

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class ListItem;
class ProgressIndicator;
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
    void BrowseInstallDirectory();
    void OpenInstallDirectory();
    void SaveMihomoSettings();
    void SyncMihomoSettings();
    void ApplyKernelUpdate(const app::AppStateSnapshot& state);

    Button* lightButton_ = nullptr;
    Button* darkButton_ = nullptr;
    std::array<Button*, 4> themeButtons_{};
    Switch* animations_ = nullptr;
    Switch* reducedMotion_ = nullptr;
    SearchField* kernelPathField_ = nullptr;
    SearchField* apiPortField_ = nullptr;
    SearchField* secretField_ = nullptr;
    Switch* autoStart_ = nullptr;
    Switch* systemProxy_ = nullptr;
    TextBlock* systemProxyStatus_ = nullptr;
    SearchField* testUrlField_ = nullptr;
    SearchField* testTimeoutField_ = nullptr;
    SearchField* installDirField_ = nullptr;
    SearchField* downloadProxyField_ = nullptr;
    TextBlock* kernelStatusLine_ = nullptr;
    TextBlock* kernelUpdateLine_ = nullptr;
    TextBlock* releaseLine_ = nullptr;
    ProgressIndicator* updateProgress_ = nullptr;
    Button* checkUpdateButton_ = nullptr;
    Button* downloadButton_ = nullptr;
    Button* cancelUpdateButton_ = nullptr;
    Button* useKernelButton_ = nullptr;
    bool mihomoFieldsInitialized_ = false;
    bool updateApplied_ = false;
    const runtime::KernelUpdateSnapshot* shownUpdate_ = nullptr;
};

}  // namespace clashyuka::ui
