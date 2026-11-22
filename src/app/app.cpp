#include "app/app.h"

#include "clashyuka/ui/main_window.h"

namespace clashyuka::app {

bool ClashYukaApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    SetAppName("ClashYuka");

    auto* frame = new ui::MainWindow();
    frame->Show();
    return true;
}

}  // namespace clashyuka::app
