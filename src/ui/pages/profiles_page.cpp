#include "clashyuka/ui/pages/profiles_page.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include <wx/datetime.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/wrapsizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

wxString RefreshText(const app::ProfileRow& row) {
    using Outcome = runtime::ProfileRefreshOutcome;
    wxString text;
    switch (row.refreshOutcome) {
        case Outcome::Never: text = "Never refreshed"; break;
        case Outcome::Succeeded: text = "YAML checked and saved"; break;
        case Outcome::NetworkFailed: text = "Network error"; break;
        case Outcome::HttpFailed: text = "HTTP error"; break;
        case Outcome::InvalidContent: text = "Invalid or unsupported YAML"; break;
        case Outcome::WriteFailed: text = "Could not save refresh"; break;
        case Outcome::Cancelled: text = "Cancelled"; break;
    }
    if (row.refreshCompletedAt > 0) {
        const wxDateTime completed(static_cast<std::time_t>(row.refreshCompletedAt));
        if (completed.IsValid()) {
            text += " (" + completed.Format("%Y-%m-%d %H:%M") + ")";
        }
    }
    return row.refreshing ? "Refreshing... Last result: " + text : text;
}

}  // namespace

ProfilesPage::ProfilesPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Profiles) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* card = pages::MakeCard(this);
    auto* summary = new wxBoxSizer(wxVERTICAL);
    nextStart_ = new TextBlock(card, "Next start: Unavailable", TypeStyle::Body);
    running_ = new TextBlock(card, "Running: None", TypeStyle::Body);
    status_ = new TextBlock(card, "", TypeStyle::Label, 190);
    availability_ = new TextBlock(card, "", TypeStyle::Label, 170);
    summary->Add(nextStart_, 0, wxEXPAND);
    summary->Add(running_, 0, wxEXPAND);
    summary->Add(status_, 0, wxEXPAND);
    summary->Add(availability_, 0, wxEXPAND);
    pages::AddSpacer(summary, card, SpaceDp(Space::Md));

    auto* intake = new wxWrapSizer(wxHORIZONTAL);
    const int gap = FromDIP(SpaceDp(Space::Sm));
    import_ = new Button(card, "Import YAML", ButtonVariant::FilledTonal);
    import_->SetOnClick([this] {
        wxFileDialog dialog(this, "Import Mihomo YAML", {}, {},
                            "YAML files (*.yaml;*.yml)|*.yaml;*.yml|All files (*.*)|*.*",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) {
            Controller().ImportProfile(dialog.GetPath());
        }
    });
    add_ = new Button(card, "Add subscription", ButtonVariant::Outlined);
    add_->SetOnClick([this] { EditSubscription(true); });
    cancelAdd_ = new Button(card, "Cancel add", ButtonVariant::Text);
    cancelAdd_->SetOnClick([this] { Controller().CancelSubscription({}); });
    intake->Add(import_, 0, wxRIGHT, gap);
    intake->Add(add_, 0, wxRIGHT, gap);
    intake->Add(cancelAdd_);
    summary->Add(intake, 0, wxEXPAND);
    card->SetContent(summary, 20);
    sizer->Add(card, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));
    sizer->Add(pages::MakeSectionTitle(this, "Saved profiles"), 0, wxEXPAND);
    empty_ = new TextBlock(this, "", TypeStyle::Label, 170);
    sizer->Add(empty_, 0, wxEXPAND);
    listSizer_ = new wxBoxSizer(wxVERTICAL);
    sizer->Add(listSizer_, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    auto* actions = new wxWrapSizer(wxHORIZONTAL);
    auto button = [&](const wxString& label, ButtonVariant variant, auto action) {
        auto* control = new Button(this, label, variant);
        control->SetOnClick(action);
        actions->Add(control, 0, wxRIGHT | wxBOTTOM, gap);
        return control;
    };
    activate_ = button("Select for next start", ButtonVariant::Filled, [this] {
        Controller().ActivateProfile(std::string(focusedId_));
    });
    rename_ = button("Rename", ButtonVariant::Outlined, [this] {
        const auto id = focusedId_;
        wxTextEntryDialog dialog(this, "Profile name", "Rename profile", profiles_->NameFor(id, {}));
        if (dialog.ShowModal() == wxID_OK) {
            Controller().RenameProfile(id, dialog.GetValue());
        }
    });
    remove_ = button("Delete", ButtonVariant::Text, [this] {
        Controller().DeleteProfile(std::string(focusedId_));
    });
    edit_ = button("Edit subscription", ButtonVariant::Outlined, [this] { EditSubscription(false); });
    refresh_ = button("Refresh", ButtonVariant::Outlined, [this] {
        Controller().RefreshSubscription(std::string(focusedId_));
    });
    cancelRefresh_ = button("Cancel download", ButtonVariant::Text, [this] {
        Controller().CancelSubscription(std::string(focusedId_));
    });
    sizer->Add(actions, 0, wxEXPAND);
    SetSizer(sizer);
    SyncState();
}

void ProfilesPage::EditSubscription(bool adding) {
    const auto id = focusedId_;
    std::optional<runtime::SubscriptionSettings> settings = adding
        ? std::optional(runtime::SubscriptionSettings{}) : Controller().SubscriptionForEdit(id);
    if (!settings) {
        return;
    }
    wxDialog dialog(this, wxID_ANY, adding ? "Add subscription" : "Edit subscription");
    auto* form = new wxBoxSizer(wxVERTICAL);
    const auto field = [&](const wxString& label, wxWindow* control) {
        form->Add(new TextBlock(&dialog, label, TypeStyle::Label), 0, wxEXPAND);
        form->Add(control, 0, wxEXPAND | wxBOTTOM, dialog.FromDIP(12));
    };
    wxTextCtrl* name = nullptr;
    if (adding) {
        name = new wxTextCtrl(&dialog, wxID_ANY);
        field("Name", name);
    }
    auto* url = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(settings->url),
                               wxDefaultPosition, dialog.FromDIP(wxSize(400, -1)));
    field("HTTPS URL", url);
    form->Add(new TextBlock(&dialog, "The URL may contain a token and is stored locally in plain text.",
                            TypeStyle::Label), 0, wxEXPAND | wxBOTTOM, dialog.FromDIP(12));
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(settings->refreshInterval);
    const int initial = static_cast<int>(std::clamp<long long>(minutes.count(), 0, 525600));
    auto* interval = new wxSpinCtrl(&dialog, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, 0, 525600, initial);
    field("Auto-refresh interval in minutes (0 = off)", interval);
    form->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(form, 1, wxEXPAND | wxALL, dialog.FromDIP(16));
    dialog.SetSizerAndFit(outer);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    settings->url = url->GetValue().Trim().Trim(false).utf8_string();
    if (adding || interval->GetValue() != initial) {
        settings->refreshInterval = std::chrono::minutes(interval->GetValue());
    }
    if (adding) {
        Controller().AddSubscription(name->GetValue(), *settings);
    } else {
        Controller().EditSubscription(id, *settings);
    }
}

void ProfilesPage::ApplyState(const app::AppStateSnapshot& state) {
    if (profiles_ == state.profiles && kernelState_ == state.kernel && !items_.empty()) {
        return;
    }
    const bool rebuild = profiles_ != state.profiles && (!profiles_ || !state.profiles ||
                          profiles_->rows.size() != state.profiles->rows.size() ||
                          !std::ranges::equal(profiles_->rows, state.profiles->rows, {},
                                              &app::ProfileRow::id, &app::ProfileRow::id));
    profiles_ = state.profiles;
    kernelState_ = state.kernel;
    if (rebuild) {
        listSizer_->Clear(true);
        items_.clear();
        if (profiles_) {
            if (std::ranges::find(profiles_->rows, focusedId_, &app::ProfileRow::id) == profiles_->rows.end()) {
                focusedId_ = profiles_->selectedProfileId;
                if (std::ranges::find(profiles_->rows, focusedId_, &app::ProfileRow::id) == profiles_->rows.end() &&
                    !profiles_->rows.empty()) {
                    focusedId_ = profiles_->rows.front().id;
                }
            }
            for (const auto& row : profiles_->rows) {
                auto* item = new ListItem(this);
                item->SetShowChevron(false);
                item->SetShowDivider(true);
                item->SetOnClick([this, id = row.id] {
                    focusedId_ = id;
                    UpdateActions(State());
                    RequestRelayout();
                });
                listSizer_->Add(item, 0, wxEXPAND);
                items_.push_back(item);
            }
        }
    }
    const bool available = profiles_ && profiles_->storeError.empty();
    const bool remote = available && profiles_->subscriptionsAvailable;
    nextStart_->SetText("Next start: " + (available
        ? profiles_->NameFor(profiles_->selectedProfileId, "Missing profile") : wxString("Unavailable")));
    wxString running = "None";
    if (profiles_ && !profiles_->runningProfileId.empty()) {
        running = profiles_->NameFor(profiles_->runningProfileId, "Unknown");
        if (profiles_->runningCopyOutdated) {
            running += " (new copy ready to apply)";
        }
    }
    running_->SetText("Running: " + running);
    const wxString status = profiles_ ? wxString::FromUTF8(profiles_->storeError.empty()
        ? profiles_->actionStatus : profiles_->storeError) : wxString("Opening profile catalog...");
    status_->SetText(status);
    status_->Show(!status.empty());
    availability_->SetText("HTTPS subscriptions are unavailable on this platform. Local YAML can still be imported.");
    availability_->Show(available && !remote);
    empty_->SetText("No imported profiles yet. You can use Built-in or import a YAML file.");
    empty_->Show(available && profiles_->rows.size() == 1);
    import_->Enable(available);
    add_->Enable(remote && !profiles_->addingSubscription);
    cancelAdd_->Show(remote && profiles_->addingSubscription);

    if (profiles_) {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const auto& row = profiles_->rows[i];
            auto* item = items_[i];
            item->SetTitle(wxString::FromUTF8(row.name));
            const bool next = row.id == profiles_->selectedProfileId;
            const bool active = row.id == profiles_->runningProfileId;
            item->SetTrailingText(next && active ? "Running / Next start" : next ? "Next start"
                                                                                 : active ? "Running" : "");
            switch (row.kind) {
                case runtime::ProfileKind::BuiltIn: item->SetSubtitle("Built-in"); break;
                case runtime::ProfileKind::Local: item->SetSubtitle("Local YAML"); break;
                case runtime::ProfileKind::Subscription:
                    item->SetSubtitle("Subscription | " + RefreshText(row));
                    break;
            }
        }
    }
    UpdateActions(state);
    RequestRelayout();
}

void ProfilesPage::UpdateActions(const app::AppStateSnapshot& state) {
    const app::ProfileRow* row = nullptr;
    if (profiles_) {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const bool focused = profiles_->rows[i].id == focusedId_;
            items_[i]->SetSelected(focused, false);
            if (focused) {
                row = &profiles_->rows[i];
            }
        }
    }
    const bool live = state.kernel == app::KernelState::Running;
    const bool stopped = state.kernel == app::KernelState::NotStarted || state.kernel == app::KernelState::Error;
    const bool next = row && row->id == profiles_->selectedProfileId;
    const bool running = row && row->id == profiles_->runningProfileId;
    const bool retrySave = live && running && !next;
    const bool subscription = row && row->kind == runtime::ProfileKind::Subscription;
    const bool userProfile = row && row->kind != runtime::ProfileKind::BuiltIn;
    const bool switching = profiles_ && profiles_->switching;
    activate_->SetLabel(retrySave ? "Retry saving selection" : live ? "Apply profile" : "Select for next start");
    activate_->Enable(row && !switching && ((stopped && !next) ||
        (live && (!running || retrySave || profiles_->runningCopyOutdated))));
    rename_->Enable(userProfile);
    remove_->Enable(userProfile && !next && !running && !switching && !row->refreshing && (live || stopped));
    edit_->Show(subscription);
    refresh_->Show(subscription);
    cancelRefresh_->Show(subscription && row->refreshing);
    edit_->Enable(subscription && profiles_->subscriptionsAvailable && !row->refreshing);
    refresh_->Enable(subscription && profiles_->subscriptionsAvailable && !row->refreshing);
}

}  // namespace clashyuka::ui
