#include "app/app.h"

#include "clashyuka/ui/demo_frame.h"

namespace clashyuka::app {

bool ClashYukaApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    SetAppName("ClashYuka");

    auto* frame = new ui::DemoFrame();
    frame->Show();
    return true;
}

}  // namespace clashyuka::app
