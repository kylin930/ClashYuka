#pragma once

#include <vector>

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class SearchField;

class ProxiesPage final : public PageBase {
public:
    ProxiesPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    void ApplyFilter(const wxString& query);
    void UpdateModeButtons();

    Button* ruleButton_ = nullptr;
    Button* globalButton_ = nullptr;
    Button* directButton_ = nullptr;
    SearchField* search_ = nullptr;
    std::vector<wxWindow*> groupBlocks_;
    std::vector<wxString> groupTitles_;
    wxWindow* emptyFilterHint_ = nullptr;
};

}  // namespace clashyuka::ui
