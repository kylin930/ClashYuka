#include "clashyuka/ui/pages/logs_page.h"

#include <array>
#include <utility>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/switch.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {

LogsPage::LogsPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Logs) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    sizer->Add(status_, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    using Filter = std::pair<Button*, wxString>;
    const std::array filters{
        Filter{new Button(this, "All", ButtonVariant::FilledTonal), ""},
        Filter{new Button(this, "Info", ButtonVariant::Text), "info"},
        Filter{new Button(this, "Warn", ButtonVariant::Text), "warn"},
        Filter{new Button(this, "Error", ButtonVariant::Text), "error"},
    };
    for (const auto& [button, level] : filters) {
        button->SetOnClick([this, level, filters] {
            if (level_ == level) {
                return;
            }
            level_ = level;
            for (const auto& [filterButton, filterLevel] : filters) {
                filterButton->SetVariant(filterLevel == level_ ? ButtonVariant::FilledTonal
                                                                : ButtonVariant::Text);
            }
            Rebuild();
        });
        toolbar->Add(button, 0, wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    }
    sizer->Add(toolbar, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
    auto* search = new SearchField(this, "Search log lines");
    search->SetOnChange([this](const wxString& query) {
        query_ = query.Lower().Trim().Trim(false);
        Rebuild();
    });
    filterRow->Add(search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    auto* autoScroll = new Switch(this);
    autoScroll->SetValue(true, false);
    autoScroll->SetOnChange([this](bool value) {
        autoScrollEnabled_ = value;
        if (value) {
            ScrollToBottom();
        }
    });
    filterRow->Add(new TextBlock(this, "Auto-scroll", TypeStyle::Label, 170), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    filterRow->Add(autoScroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    clearButton_ = new Button(this, "Clear", ButtonVariant::Text);
    clearButton_->Enable(false);
    clearButton_->SetOnClick([this] { Controller().ClearLogs(); });
    filterRow->Add(clearButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(filterRow, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    output_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             FromDIP(wxSize(160, 340)),
                             wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
    output_->SetName("Mihomo logs");
    output_->SetMaxLength(500 * (4096 + 32 + 5));
    sizer->Add(output_, 1, wxEXPAND);
    SetSizer(sizer);
    SyncState();
    Rebuild();
}

void LogsPage::ApplyState(const app::AppStateSnapshot& state) {
    const auto& live = state.live;
    if (!live.running) {
        const wxString text = live.logs ? "Mihomo is stopped. Showing the last session's logs."
                                        : "Logs will appear when Mihomo is running.";
        if (status_->GetLabel() != text) {
            status_->SetLabel(text);
        }
    } else {
        wxString text;
        switch (live.logsStatus) {
            case runtime::StreamAvailability::Waiting: text = "Waiting for Mihomo logs."; break;
            case runtime::StreamAvailability::Available: text = "Live Mihomo logs"; break;
            case runtime::StreamAvailability::Disconnected: text = "Mihomo log stream interrupted."; break;
            case runtime::StreamAvailability::Disabled: text = "Mihomo log stream is disabled."; break;
        }
        if (status_->GetLabel() != text) {
            status_->SetLabel(text);
        }
    }
    if (entries_ != live.logs) {
        entries_ = live.logs;
        clearButton_->Enable(entries_ != nullptr);
        Rebuild();
    }
}

void LogsPage::Rebuild() {
    wxString text;
    if (entries_) {
        for (const auto& entry : *entries_) {
            const wxString level = wxString::FromUTF8(entry.type).Lower();
            if (!level_.empty() && level != level_ && !(level_ == "warn" && level == "warning")) {
                continue;
            }
            const wxString line = "[" + level + "] " + wxString::FromUTF8(entry.payload);
            if (query_.empty() || line.Lower().Contains(query_)) {
                text += line + "\n";
            }
        }
    }
    if (text.empty()) {
        text = entries_ ? "No log entries match the current filter." : "The log is empty.";
    }
    if (text == renderedText_) {
        return;
    }
    renderedText_ = std::move(text);

#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(output_->GetHandle());
    const auto firstLine = ::SendMessage(handle, EM_GETFIRSTVISIBLELINE, 0, 0);
#endif
    wxWindowUpdateLocker lock(output_);
    output_->ChangeValue(renderedText_);
    if (autoScrollEnabled_) {
        ScrollToBottom();
    }
#ifdef __WXMSW__
    else {
        const auto current = ::SendMessage(handle, EM_GETFIRSTVISIBLELINE, 0, 0);
        ::SendMessage(handle, EM_LINESCROLL, 0, firstLine - current);
    }
#endif
}

void LogsPage::ScrollToBottom() {
    output_->ShowPosition(output_->GetLastPosition());
}

}  // namespace clashyuka::ui
