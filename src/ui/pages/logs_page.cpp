#include "clashyuka/ui/pages/logs_page.h"

#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/switch.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {

LogsPage::LogsPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Logs) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             wxString::FromUTF8("Demo log preview — entries below are local "
                                                "samples, not output from Mihomo."),
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    allButton_ = new Button(this, "All", ButtonVariant::FilledTonal);
    infoButton_ = new Button(this, "Info", ButtonVariant::Text);
    warnButton_ = new Button(this, "Warn", ButtonVariant::Text);
    errorButton_ = new Button(this, "Error", ButtonVariant::Text);
    allButton_->SetOnClick([this] {
        filter_ = Filter::All;
        UpdateFilters();
        Rebuild();
    });
    infoButton_->SetOnClick([this] {
        filter_ = Filter::Info;
        UpdateFilters();
        Rebuild();
    });
    warnButton_->SetOnClick([this] {
        filter_ = Filter::Warn;
        UpdateFilters();
        Rebuild();
    });
    errorButton_->SetOnClick([this] {
        filter_ = Filter::Error;
        UpdateFilters();
        Rebuild();
    });
    for (Button* button : {allButton_, infoButton_, warnButton_, errorButton_}) {
        toolbar->Add(button, 0, wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    }
    sizer->Add(toolbar, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
    search_ = new SearchField(this, "Search log lines");
    search_->SetOnChange([this](const wxString& query) {
        query_ = query;
        Rebuild();
    });
    filterRow->Add(search_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    autoScroll_ = new Switch(this);
    autoScroll_->SetValue(true, false);
    autoScroll_->SetOnChange([this](bool value) {
        autoScrollEnabled_ = value;
        if (autoScrollEnabled_) {
            ScrollToBottom();
        }
    });
    filterRow->Add(new TextBlock(this, "Auto-scroll", TypeStyle::Label, 170), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    filterRow->Add(autoScroll_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(SpaceDp(Space::Md)));
    auto* clearButton = new Button(this, "Clear", ButtonVariant::Text);
    clearButton->SetOnClick([this] {
        entries_.clear();
        Rebuild();
        Controller().ShowMessage("Demo log cleared");
    });
    filterRow->Add(clearButton, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(filterRow, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    logCard_ = pages::MakeCard(this, SurfaceLevel::Lowest);
    logSizer_ = new wxBoxSizer(wxVERTICAL);
    logCard_->SetContent(logSizer_, 16);
    sizer->Add(logCard_, 1, wxEXPAND);

    entries_ = {
        {Level::Info, "[info] Application shell initialised"},
        {Level::Info, "[info] Theme: Purple, light mode"},
        {Level::Warn, "[warn] Mihomo core is not running"},
        {Level::Info, "[info] API connection: disconnected"},
        {Level::Info, "[info] Waiting for the kernel integration stage (P2.2)"},
    };

    SetSizer(sizer);
    SyncState();
    Rebuild();
}

void LogsPage::ApplyState(const app::AppStateSnapshot&) {
    // Log streaming is wired in P2.2; nothing to reflect yet.
}

void LogsPage::UpdateFilters() {
    const auto variantFor = [](bool active) {
        return active ? ButtonVariant::FilledTonal : ButtonVariant::Text;
    };
    allButton_->SetVariant(variantFor(filter_ == Filter::All));
    infoButton_->SetVariant(variantFor(filter_ == Filter::Info));
    warnButton_->SetVariant(variantFor(filter_ == Filter::Warn));
    errorButton_->SetVariant(variantFor(filter_ == Filter::Error));
}

void LogsPage::Rebuild() {
    if (logSizer_ == nullptr) {
        return;
    }
    logSizer_->Clear(true);

    const wxString query = query_.Lower().Trim();
    int shown = 0;
    for (const Entry& entry : entries_) {
        const bool levelMatches =
            filter_ == Filter::All ||
            (filter_ == Filter::Info && entry.level == Level::Info) ||
            (filter_ == Filter::Warn && entry.level == Level::Warn) ||
            (filter_ == Filter::Error && entry.level == Level::Error);
        if (!levelMatches) {
            continue;
        }
        if (!query.empty() && !entry.text.Lower().Contains(query)) {
            continue;
        }
        const int alpha = entry.level == Level::Error
                              ? 255
                              : (entry.level == Level::Warn ? 220 : 180);
        logSizer_->Add(new TextBlock(logCard_, entry.text, TypeStyle::Label, alpha), 0,
                       wxEXPAND | wxBOTTOM, FromDIP(SpaceDp(Space::Xs)));
        ++shown;
    }
    if (shown == 0) {
        logSizer_->Add(new TextBlock(logCard_,
                                     entries_.empty()
                                         ? "The log is empty."
                                         : "No log entries match the current filter.",
                                     TypeStyle::Body, 170),
                       0, wxEXPAND);
    }

    logCard_->Layout();
    RequestRelayout();
    if (autoScrollEnabled_) {
        ScrollToBottom();
    }
}

void LogsPage::ScrollToBottom() {
    wxWindow* window = GetParent();
    while (window != nullptr) {
        if (auto* scrolled = wxDynamicCast(window, wxScrolledWindow)) {
            scrolled->Layout();
            scrolled->FitInside();
            scrolled->Scroll(0, scrolled->GetScrollRange(wxVERTICAL));
            break;
        }
        window = window->GetParent();
    }
}

}  // namespace clashyuka::ui
