#pragma once

#include <vector>

#include <wx/string.h>

#include "clashyuka/ui/pages/page_base.h"

class wxSizer;

namespace clashyuka::ui {

class Button;
class SearchField;
class SurfaceCard;
class Switch;
class TextBlock;

class LogsPage final : public PageBase {
public:
    LogsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    enum class Level { Info, Warn, Error };
    enum class Filter { All, Info, Warn, Error };

    struct Entry {
        Level level = Level::Info;
        wxString text;
    };

    void Rebuild();
    void UpdateFilters();
    void ScrollToBottom();

    Button* allButton_ = nullptr;
    Button* infoButton_ = nullptr;
    Button* warnButton_ = nullptr;
    Button* errorButton_ = nullptr;
    SearchField* search_ = nullptr;
    Switch* autoScroll_ = nullptr;
    SurfaceCard* logCard_ = nullptr;
    wxSizer* logSizer_ = nullptr;
    Filter filter_ = Filter::All;
    bool autoScrollEnabled_ = true;
    wxString query_;
    std::vector<Entry> entries_;
};

}  // namespace clashyuka::ui
