#pragma once

#include <functional>
#include <vector>

#include <wx/string.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/ui/drawing/icons.h"

class wxWindow;

namespace clashyuka::ui {
class PageBase;
}

namespace clashyuka::app {

class AppController;

struct PageDescriptor {
    PageId id;
    wxString title;
    wxString description;
    ui::IconId icon;
    std::function<ui::PageBase*(wxWindow* parent, AppController& controller)> create;
};

// Central list of application pages. The rail, the header and the page
// container all derive their content from this registry.
class PageRegistry {
public:
    static const std::vector<PageDescriptor>& All();
    static const PageDescriptor& Get(PageId id);
    static int IndexOf(PageId id);
};

const char* PageIdName(PageId id);

}  // namespace clashyuka::app
