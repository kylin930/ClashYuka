#include "app/app.h"

#include "ui/main_frame.h"

namespace clashyuka::app {

bool ClashYukaApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    SetAppName("ClashYuka");

    auto* frame = new ui::MainFrame();
    frame->Show();
    return true;
}

}  // namespace clashyuka::app
