#pragma once

#include "clashyuka/ui/pages/page_base.h"

class wxStaticText;
class wxTextCtrl;

namespace clashyuka::ui {

class Button;

class LogsPage final : public PageBase {
public:
    LogsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    void Rebuild();
    void ScrollToBottom();

    wxStaticText* status_ = nullptr;
    wxTextCtrl* output_ = nullptr;
    Button* clearButton_ = nullptr;
    wxString level_;
    wxString query_;
    bool autoScrollEnabled_ = true;
    std::shared_ptr<const std::vector<mihomo::MihomoLogEntry>> entries_;
    wxString renderedText_;
};

}  // namespace clashyuka::ui
