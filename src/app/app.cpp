#include "app/app.h"

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
    frame->Show();
    core::MarkStartup("app-first-window-shown");
    return true;
}

}  // namespace clashyuka::app
