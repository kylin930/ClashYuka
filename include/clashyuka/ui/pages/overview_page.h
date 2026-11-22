#pragma once

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class TextBlock;

class OverviewPage final : public PageBase {
public:
    OverviewPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    TextBlock* kernelValue_ = nullptr;
    TextBlock* apiValue_ = nullptr;
    TextBlock* versionValue_ = nullptr;
    TextBlock* profileValue_ = nullptr;
    TextBlock* modeValue_ = nullptr;
    TextBlock* uploadValue_ = nullptr;
    TextBlock* downloadValue_ = nullptr;
    TextBlock* trafficNote_ = nullptr;
};

}  // namespace clashyuka::ui
