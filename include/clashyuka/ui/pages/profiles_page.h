#pragma once

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class TextBlock;

class ProfilesPage final : public PageBase {
public:
    ProfilesPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    TextBlock* profileValue_ = nullptr;
    TextBlock* statusValue_ = nullptr;
    TextBlock* detailsValue_ = nullptr;
};

}  // namespace clashyuka::ui
