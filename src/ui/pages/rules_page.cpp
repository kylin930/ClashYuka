#include "clashyuka/ui/pages/rules_page.h"

#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/wupdlock.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

using runtime::RuleProviderUpdateStatus;
using runtime::RulesLoadStatus;

wxString CollectionStatus(bool available, bool switching, bool loading, RulesLoadStatus status,
                          bool hasData, bool empty, const wxString& label) {
    if (!available) {
        if (switching) {
            return "Waiting for the profile switch.";
        }
        return "Start Mihomo to view " + label + ".";
    }
    if (loading || status == RulesLoadStatus::Idle) {
        return "Loading " + label + "...";
    }
    if (status == RulesLoadStatus::Failed) {
        return "Could not load " + label + (hasData ? ". Showing last results. Reload to retry."
                                                     : ". Reload to retry.");
    }
    return empty ? "Mihomo reported no " + label + "." : wxString{};
}

wxString UpdateStatus(const runtime::RulesSnapshot& rules) {
    switch (rules.updateStatus) {
        case RuleProviderUpdateStatus::Idle: return {};
        case RuleProviderUpdateStatus::Updating: return "Refreshing the selected provider...";
        case RuleProviderUpdateStatus::Updated: return "Provider refreshed. Mihomo data reloaded.";
        case RuleProviderUpdateStatus::UpdateFailed: return "Provider refresh failed. Try again.";
        case RuleProviderUpdateStatus::ReadbackFailed:
            return "Refresh could not be verified. Reload Mihomo data to retry.";
    }
    return {};
}

}  // namespace

class RuleList final : public wxListCtrl {
public:
    explicit RuleList(wxWindow* parent)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(180, 250)),
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL) {
        AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(125));
        AppendColumn("Match", wxLIST_FORMAT_LEFT, FromDIP(250));
        AppendColumn("Target", wxLIST_FORMAT_LEFT, FromDIP(160));
        SetName("Active rules");
    }

    void Replace(std::shared_ptr<const mihomo::RuleCollection> rules, const wxString& query) {
        wxWindowUpdateLocker lock(this);
        rules_ = std::move(rules);
        matches_.clear();
        if (rules_ && !query.empty()) {
            for (std::size_t i = 0; i < rules_->size(); ++i) {
                const auto& rule = (*rules_)[i];
                if (wxString::FromUTF8(rule.type).Lower().Contains(query) ||
                    wxString::FromUTF8(rule.payload).Lower().Contains(query) ||
                    wxString::FromUTF8(rule.proxy).Lower().Contains(query)) {
                    matches_.push_back(i);
                }
            }
        }
        filtered_ = !query.empty();
        const auto size = filtered_ ? matches_.size() : rules_ ? rules_->size() : 0;
        SetItemCount(static_cast<long>(size));
        Refresh();
    }

private:
    wxString OnGetItemText(long item, long column) const override {
        if (!rules_ || item < 0) {
            return {};
        }
        const auto index = static_cast<std::size_t>(item);
        if (index >= (filtered_ ? matches_.size() : rules_->size())) {
            return {};
        }
        const auto& rule = (*rules_)[filtered_ ? matches_[index] : index];
        switch (column) {
            case 0: return wxString::FromUTF8(rule.type);
            case 1: return wxString::FromUTF8(rule.payload);
            case 2: return wxString::FromUTF8(rule.proxy);
            default: return {};
        }
    }

    std::shared_ptr<const mihomo::RuleCollection> rules_;
    std::vector<std::size_t> matches_;
    bool filtered_ = false;
};

RulesPage::RulesPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Rules) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* ruleToolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* search = new SearchField(this, "Search rule types, matches, or targets");
    search->SetOnChange([this](const wxString& query) {
        query_ = query.Lower().Trim().Trim(false);
        RebuildRules();
    });
    ruleToolbar->Add(search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    reload_ = new Button(this, "Reload", ButtonVariant::FilledTonal);
    reload_->SetOnClick([this] { Controller().RefreshRules(); });
    ruleToolbar->Add(reload_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(ruleToolbar, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));
    rulesStatus_ = new TextBlock(this, "", TypeStyle::Body);
    sizer->Add(rulesStatus_, 0, wxEXPAND);
    ruleList_ = new RuleList(this);
    sizer->Add(ruleList_, 3, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* providerToolbar = new wxBoxSizer(wxHORIZONTAL);
    providerToolbar->Add(new TextBlock(this, "Rule providers", TypeStyle::Title), 1,
                         wxALIGN_CENTER_VERTICAL);
    update_ = new Button(this, "Refresh provider", ButtonVariant::FilledTonal);
    update_->SetOnClick([this] {
        const auto index = providerList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (index >= 0 && rules_.providers && static_cast<std::size_t>(index) < rules_.providers->providers.size()) {
            Controller().RefreshRuleProvider(rules_.providers->providers[index].name);
        }
    });
    providerToolbar->Add(update_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(providerToolbar, 0, wxEXPAND);
    providerStatus_ = new TextBlock(this, "", TypeStyle::Body);
    updateStatus_ = new TextBlock(this, "", TypeStyle::Body);
    sizer->Add(providerStatus_, 0, wxEXPAND);
    sizer->Add(updateStatus_, 0, wxEXPAND);
    providerList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(180, 140)),
                                   wxLC_REPORT | wxLC_SINGLE_SEL);
    providerList_->SetName("Rule providers");
    providerList_->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(145));
    providerList_->AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(150));
    providerList_->AppendColumn("Rules", wxLIST_FORMAT_RIGHT, FromDIP(60));
    providerList_->AppendColumn("Updated at", wxLIST_FORMAT_LEFT, FromDIP(235));
    providerList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateProviderAction(); });
    providerList_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { UpdateProviderAction(); });
    sizer->Add(providerList_, 2, wxEXPAND);
    SetSizer(sizer);
    OnThemeChanged();
    SyncState();
}

void RulesPage::OnThemeChanged() {
    PageBase::OnThemeChanged();
    const auto& colors = ThemeManager::Instance().Colors();
    for (auto* list : {static_cast<wxListCtrl*>(ruleList_), providerList_}) {
        list->SetBackgroundColour(ToWx(colors.surface));
        list->SetTextColour(ToWx(colors.onSurface));
        list->Refresh();
    }
}

void RulesPage::ApplyState(const app::AppStateSnapshot& state) {
    if (initialized_ && available_ == state.rulesAvailable && switching_ == state.live.switching &&
        rules_ == state.rules) {
        return;
    }
    const bool providersChanged = rules_.providers != state.rules.providers;
    initialized_ = true;
    available_ = state.rulesAvailable;
    switching_ = state.live.switching;
    const bool rulesChanged = rules_.rules != state.rules.rules;
    rules_ = state.rules;
    if (rulesChanged) {
        RebuildRules();
    } else {
        UpdateRulesStatus();
    }
    if (providersChanged) {
        RebuildProviders();
    }
    providerStatus_->SetText(CollectionStatus(available_, switching_, rules_.loading, rules_.providersStatus,
                              !!rules_.providers, rules_.providers && rules_.providers->providers.empty(),
                              "rule providers"));
    updateStatus_->SetText(UpdateStatus(rules_));
    reload_->Enable(available_ && !rules_.loading);
    UpdateProviderAction();
    RequestRelayout();
}

void RulesPage::RebuildRules() {
    ruleList_->Replace(rules_.rules, query_);
    UpdateRulesStatus();
}

void RulesPage::UpdateRulesStatus() {
    auto status = CollectionStatus(available_, switching_, rules_.loading, rules_.rulesStatus,
                                   !!rules_.rules, rules_.rules && rules_.rules->empty(), "rules");
    if (status.empty()) {
        const long count = ruleList_->GetItemCount();
        if (query_.empty()) {
            status = wxString::Format("%ld active rules", count);
        } else if (count == 0) {
            status = "No rules match your search.";
        } else {
            status = wxString::Format("%ld matching rules", count);
        }
    }
    rulesStatus_->SetText(status);
}

void RulesPage::RebuildProviders() {
    wxWindowUpdateLocker lock(providerList_);
    const auto selected = providerList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    const auto selectedName = selected < 0 ? wxString{} : providerList_->GetItemText(selected);
    providerList_->DeleteAllItems();
    if (!rules_.providers) {
        return;
    }
    for (const auto& provider : rules_.providers->providers) {
        const auto name = wxString::FromUTF8(provider.name);
        const auto index = providerList_->InsertItem(providerList_->GetItemCount(), name);
        auto type = wxString::FromUTF8(provider.behavior.empty() ? provider.type : provider.behavior);
        if (!provider.vehicleType.empty()) {
            type += " / " + wxString::FromUTF8(provider.vehicleType);
        }
        providerList_->SetItem(index, 1, type);
        providerList_->SetItem(index, 2, wxString::Format("%d", provider.ruleCount));
        providerList_->SetItem(index, 3, provider.updatedAt.empty() ? wxString("Unknown")
                                                                   : wxString::FromUTF8(provider.updatedAt));
        if (name == selectedName && selected >= 0) {
            providerList_->SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }
    }
}

void RulesPage::UpdateProviderAction() {
    const auto selected = providerList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    update_->Enable(available_ && !rules_.loading && rules_.providers && selected >= 0 &&
                    static_cast<std::size_t>(selected) < rules_.providers->providers.size() &&
                    rules_.providers->providers[selected].CanRefresh());
}

}  // namespace clashyuka::ui
