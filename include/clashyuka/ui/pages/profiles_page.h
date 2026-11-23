#pragma once

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class ListItem;
class TextBlock;

class ProfilesPage final : public PageBase {
public:
    ProfilesPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    void EditSubscription(bool adding);
    void UpdateActions(const app::AppStateSnapshot& state);

    std::shared_ptr<const app::ProfilesSnapshot> profiles_;
    std::string focusedId_;
    wxSizer* listSizer_ = nullptr;
    std::vector<ListItem*> items_;
    TextBlock* nextStart_ = nullptr;
    TextBlock* running_ = nullptr;
    TextBlock* status_ = nullptr;
    TextBlock* availability_ = nullptr;
    TextBlock* empty_ = nullptr;
    Button* import_ = nullptr;
    Button* add_ = nullptr;
    Button* cancelAdd_ = nullptr;
    Button* activate_ = nullptr;
    Button* rename_ = nullptr;
    Button* remove_ = nullptr;
    Button* edit_ = nullptr;
    Button* refresh_ = nullptr;
    Button* cancelRefresh_ = nullptr;
    app::KernelState kernelState_ = app::KernelState::NotStarted;
};

}  // namespace clashyuka::ui
