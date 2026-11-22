#pragma once

#include <wx/app.h>

namespace clashyuka::app {

class ClashYukaApp final : public wxApp {
public:
    bool OnInit() override;
};

}  // namespace clashyuka::app
