#include "app/app.h"

#include <wx/event.h>

#include "clashyuka/core/timing.h"
#include "clashyuka/ui/main_window.h"

namespace clashyuka::app {

bool ClashYukaApp::OnInit() {
    core::MarkStartup("app-init");
    if (!wxApp::OnInit()) {
        return false;
    }

    SetAppName("ClashYuka");

    auto* frame = new ui::MainWindow();
#ifdef __WXMSW__
    // wxWidgets normally sends vetoable close events to every top-level window
    // during the query. Let the actual end-session event run window cleanup instead.
    Bind(wxEVT_QUERY_END_SESSION, [](wxCloseEvent&) {});
#endif
    frame->Show();
    core::MarkStartup("app-first-window-shown");
    return true;
}

}  // namespace clashyuka::app
