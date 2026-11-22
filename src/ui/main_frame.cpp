#include "ui/main_frame.h"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/version.h>

#include "clashyuka/core/app_info.h"

namespace clashyuka::ui {

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "ClashYuka", wxDefaultPosition, wxSize(460, 240)) {
    const core::RuntimeInfo info = core::GetRuntimeInfo();

    auto* panel = new wxPanel(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, "ClashYuka");
    sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto addLine = [&](const wxString& text) {
        auto* line = new wxStaticText(panel, wxID_ANY, text);
        sizer->Add(line, 0, wxLEFT | wxRIGHT | wxTOP, 16);
    };

    addLine("Platform: " + wxString::FromUTF8(info.platformName) +
            (info.elevated ? " (elevated)" : " (standard user)"));
    addLine("C++ standard: " + wxString::FromUTF8(info.cppStandard));
    addLine("Compiler: " + wxString::FromUTF8(info.compiler));
    addLine("wxWidgets: " + wxString::Format("%d.%d.%d", wxMAJOR_VERSION,
                                             wxMINOR_VERSION, wxRELEASE_NUMBER));

    sizer->AddSpacer(16);
    panel->SetSizer(sizer);

    Centre();
}

}  // namespace clashyuka::ui
