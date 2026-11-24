#pragma once

#include "clashyuka/ui/pages/page_base.h"

class wxListCtrl;

namespace clashyuka::ui {

class Button;
class RuleList;
class TextBlock;

class RulesPage final : public PageBase {
public:
    RulesPage(wxWindow* parent, app::AppController& controller);
    void OnThemeChanged() override;

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    void RebuildRules();
    void UpdateRulesStatus();
    void RebuildProviders();
    void UpdateProviderAction();

    runtime::RulesSnapshot rules_;
    bool available_ = false;
    bool switching_ = false;
    bool initialized_ = false;
    wxString query_;
    RuleList* ruleList_ = nullptr;
    wxListCtrl* providerList_ = nullptr;
    Button* reload_ = nullptr;
    Button* update_ = nullptr;
    TextBlock* rulesStatus_ = nullptr;
    TextBlock* providerStatus_ = nullptr;
    TextBlock* updateStatus_ = nullptr;
};

}  // namespace clashyuka::ui
