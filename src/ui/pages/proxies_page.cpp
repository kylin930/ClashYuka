#include "clashyuka/ui/pages/proxies_page.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

#include <wx/datetime.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/runtime/proxy_service.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsSelector(const std::string& type) {
    return type == "Selector";
}

bool IsAutoGroup(const std::string& type) {
    return type == "URLTest" || type == "Fallback" || type == "LoadBalance";
}

// Keep groups visible even when a provider failed to load: an empty group is
// more informative than a group that silently disappears.
bool IsGroupType(const std::string& type) {
    return IsSelector(type) || IsAutoGroup(type) || type == "Relay";
}

wxString GroupSummary(const std::string& type, const mihomo::ProxyInfo& group,
                      bool switching) {
    if (switching) {
        return "Switching selection...";
    }
    if (IsSelector(type)) {
        return group.now.empty() ? wxString("No selection")
                                 : wxString::FromUTF8("Selected: " + group.now);
    }
    if (type == "LoadBalance") {
        return wxString::Format("Load balancing across %d members",
                                static_cast<int>(group.all.size()));
    }
    if (type == "URLTest" || type == "Fallback") {
        wxString text = group.now.empty() ? wxString("Automatic selection")
                                          : wxString::FromUTF8("Automatic: " + group.now);
        if (!group.fixed.empty()) {
            text += wxString::FromUTF8("   ·   Fixed: " + group.fixed);
        }
        return text;
    }
    return wxString::FromUTF8(type + " · " + std::to_string(group.all.size()) + " members");
}

wxString NodeSubtitle(const mihomo::ProxyInfo& node) {
    wxString subtitle = wxString::FromUTF8(node.type.empty() ? "Unknown" : node.type);
    if (node.udp || node.uot || node.xudp) {
        subtitle += wxString::FromUTF8(" · UDP");
    }
    if (!node.providerName.empty()) {
        subtitle += wxString::FromUTF8(" · " + node.providerName);
    }
    return subtitle;
}

}  // namespace

ProxiesPage::ProxiesPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Proxies) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             "Proxy groups and nodes come from the running Mihomo core. "
                             "Selecting a node updates the core immediately.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    refresh_ = new Button(this, "Refresh", ButtonVariant::FilledTonal);
    refresh_->SetOnClick([this] { Controller().RefreshProxies(); });
    cancelTests_ = new Button(this, "Cancel tests", ButtonVariant::Text);
    cancelTests_->SetOnClick([this] { Controller().CancelProxyTests(); });
    search_ = new SearchField(this, "Search groups and nodes");
    search_->SetOnChange([this](const wxString& query) {
        filter_ = query;
        ApplyFilter();
    });
    toolbar->Add(refresh_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    toolbar->Add(cancelTests_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Lg)));
    toolbar->Add(search_, 1, wxALIGN_CENTER_VERTICAL);
    sizer->Add(toolbar, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));

    statusLine_ = new TextBlock(this, "Start the kernel to load proxy groups.", TypeStyle::Label,
                                170);
    sizer->Add(statusLine_, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));

    groupsHost_ = new wxPanel(this);
    groupsSizer_ = new wxBoxSizer(wxVERTICAL);
    groupsHost_->SetSizer(groupsSizer_);
    sizer->Add(groupsHost_, 0, wxEXPAND);

    emptyState_ = new TextBlock(this, "No proxy groups available.", TypeStyle::Body, 170);
    emptyState_->Show(false);
    sizer->Add(emptyState_, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void ProxiesPage::ApplyState(const app::AppStateSnapshot& state) {
    const std::shared_ptr<const runtime::ProxyCatalog> catalog = state.proxyCatalog;
    if (!catalog) {
        Clear();
        statusLine_->SetText("Start the kernel to load proxy groups.");
        emptyState_->SetText("No proxy groups available. Start the kernel and refresh.");
        emptyState_->Show(true);
        cancelTests_->Enable(false);
        RequestRelayout();
        return;
    }

    if (builtRevision_ == 0 || catalog->structureRevision != builtRevision_) {
        Rebuild(catalog);
    } else {
        UpdateDynamic(catalog);
    }
}

void ProxiesPage::Clear() {
    if (groupsSizer_ != nullptr) {
        groupsSizer_->Clear(true);
    }
    groups_.clear();
    nodeItems_.clear();
    nodeRows_.clear();
    builtRevision_ = 0;
    pendingGroup_.clear();
}

void ProxiesPage::Rebuild(const std::shared_ptr<const runtime::ProxyCatalog>& catalog) {
    Clear();
    if (!catalog) {
        return;
    }

    std::map<std::string, const mihomo::ProxyInfo*> byName;
    for (const mihomo::ProxyInfo& proxy : catalog->proxies) {
        byName[proxy.name] = &proxy;
    }

    std::vector<const mihomo::ProxyInfo*> groups;
    for (const mihomo::ProxyInfo& proxy : catalog->proxies) {
        if (IsGroupType(proxy.type) || !proxy.all.empty()) {
            groups.push_back(&proxy);
        }
    }

    for (const mihomo::ProxyInfo* group : groups) {
        const std::string type = group->type;
        auto* card = new SurfaceCard(groupsHost_, SurfaceLevel::Low);
        auto* cardSizer = new wxBoxSizer(wxVERTICAL);

        auto* header = new ListItem(card);
        header->SetTitle(wxString::FromUTF8(group->name));
        header->SetSubtitle(wxString::FromUTF8(
            (type.empty() ? std::string("Unknown") : type) + " · " +
            std::to_string(group->all.size()) + " members"));
        header->SetTrailingText(
            wxString::Format("%d", static_cast<int>(group->all.size())));
        header->SetLeadingIcon(IconId::Wifi);
        header->SetShowChevron(true);
        header->SetToolTip(wxString::FromUTF8(group->name));
        const std::string groupName = group->name;
        header->SetOnClick([this, groupName] {
            if (collapsed_.count(groupName) != 0) {
                collapsed_.erase(groupName);
            } else {
                collapsed_.insert(groupName);
            }
            ApplyFilter();
        });
        cardSizer->Add(header, 0, wxEXPAND);

        auto* summary = new TextBlock(card, GroupSummary(type, *group, false), TypeStyle::Label,
                                      170);
        cardSizer->Add(summary, 0, wxEXPAND | wxTOP, FromDIP(SpaceDp(Space::Xs)));

        auto* actions = new wxBoxSizer(wxHORIZONTAL);
        Button* testGroup = nullptr;
        Button* unsetFixed = nullptr;
        if (IsSelector(type) || IsAutoGroup(type) || !group->all.empty()) {
            testGroup = new Button(card, "Test group", ButtonVariant::Outlined);
            const std::vector<std::string> members = group->all;
            testGroup->SetOnClick([this, members] { Controller().TestGroupDelays(members); });
            actions->Add(testGroup, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
        }
        if ((type == "URLTest" || type == "Fallback") && !group->fixed.empty()) {
            unsetFixed = new Button(card, "Unset fixed", ButtonVariant::Text);
            unsetFixed->SetOnClick([this, groupName] {
                Controller().ClearProxySelection(groupName);
            });
            actions->Add(unsetFixed, 0);
        }
        if (testGroup != nullptr || unsetFixed != nullptr) {
            cardSizer->Add(actions, 0, wxEXPAND | wxTOP, FromDIP(SpaceDp(Space::Xs)));
        }

        auto* nodesPanel = new wxPanel(card);
        auto* nodeSizer = new wxBoxSizer(wxVERTICAL);
        for (const std::string& memberName : group->all) {
            const auto found = byName.find(memberName);
            mihomo::ProxyInfo unknown;
            unknown.name = memberName;
            unknown.type = "Unknown";
            const mihomo::ProxyInfo& node = found != byName.end() ? *found->second : unknown;

            auto* rowPanel = new wxPanel(nodesPanel);
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* item = new ListItem(rowPanel);
            item->SetTitle(wxString::FromUTF8(memberName));
            item->SetSubtitle(NodeSubtitle(node));
            item->SetTrailingText(wxString::FromUTF8(DelayText(*catalog, memberName)));
            item->SetShowChevron(false);
            item->SetSelected(!group->now.empty() && group->now == memberName);
            item->SetToolTip(wxString::FromUTF8(memberName));
            if (IsSelector(type)) {
                const std::string target = memberName;
                item->SetOnClick([this, groupName, target] { SelectNode(groupName, target); });
            }
            auto* test = new Button(rowPanel, "Test", ButtonVariant::Text);
            test->SetOnClick([this, memberName] { Controller().TestProxyDelay(memberName); });
            row->Add(item, 1, wxEXPAND | wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
            row->Add(test, 0, wxALIGN_CENTER_VERTICAL);
            rowPanel->SetSizer(row);
            nodeSizer->Add(rowPanel, 0, wxEXPAND);
            nodeItems_[memberName] = item;
            nodeRows_[memberName] = rowPanel;
        }
        nodesPanel->SetSizer(nodeSizer);

        cardSizer->Add(nodesPanel, 0, wxEXPAND | wxTOP, FromDIP(SpaceDp(Space::Xs)));
        card->SetContent(cardSizer, 16);
        groupsSizer_->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(SpaceDp(Space::Md)));

        GroupView view;
        view.name = groupName;
        view.type = type;
        view.members = group->all;
        view.panel = card;
        view.nodesPanel = nodesPanel;
        view.header = header;
        view.summary = summary;
        view.testGroup = testGroup;
        view.unsetFixed = unsetFixed;
        groups_.push_back(std::move(view));
    }

    builtRevision_ = catalog->structureRevision;
    pendingGroup_.clear();
    ApplyFilter();
    UpdateDynamic(catalog);
    RequestRelayout();
}

void ProxiesPage::UpdateDynamic(const std::shared_ptr<const runtime::ProxyCatalog>& catalog) {
    if (!catalog) {
        return;
    }
    for (const GroupView& group : groups_) {
        if (group.summary == nullptr) {
            continue;
        }
        const auto found = std::find_if(
            catalog->proxies.begin(), catalog->proxies.end(),
            [&group](const mihomo::ProxyInfo& proxy) { return proxy.name == group.name; });
        if (found != catalog->proxies.end()) {
            group.summary->SetText(GroupSummary(group.type, *found,
                                                pendingGroup_ == group.name));
        }
        if (group.testGroup != nullptr) {
            group.testGroup->Enable(!catalog->loading);
        }
    }

    for (const mihomo::ProxyInfo& proxy : catalog->proxies) {
        if (proxy.all.empty()) {
            continue;
        }
        for (const std::string& memberName : proxy.all) {
            const auto item = nodeItems_.find(memberName);
            if (item != nodeItems_.end()) {
                item->second->SetTrailingText(DelayText(*catalog, memberName));
            }
        }
    }
    if (!catalog->error.empty() && !pendingGroup_.empty()) {
        pendingGroup_.clear();
    }
    cancelTests_->Enable(!catalog->testing.empty());

    if (catalog->loading) {
        statusLine_->SetText("Loading proxy groups from the kernel...");
    } else if (!catalog->error.empty()) {
        statusLine_->SetText(wxString::FromUTF8("Proxy update failed: " + catalog->error));
    } else if (catalog->loaded) {
        int nodes = 0;
        int groups = 0;
        for (const mihomo::ProxyInfo& proxy : catalog->proxies) {
            if (IsGroupType(proxy.type) || !proxy.all.empty()) {
                ++groups;
            } else {
                ++nodes;
            }
        }
        wxString line = wxString::Format("%d groups", groups) + wxString::FromUTF8(" · ") +
                        wxString::Format("%d nodes", nodes);
        if (catalog->updatedAtMs > 0) {
            const wxDateTime updated(
                static_cast<time_t>(catalog->updatedAtMs / 1000));
            line += wxString::FromUTF8(" · updated ") + updated.Format("%H:%M:%S");
        }
        statusLine_->SetText(line);
    } else {
        statusLine_->SetText("Refresh to load proxy groups.");
    }
}

void ProxiesPage::ApplyFilter() {
    const wxString lowered = filter_.Lower().Trim();
    const bool filtering = !lowered.empty();
    bool anyVisible = false;
    bool anyGroups = false;

    for (const GroupView& group : groups_) {
        if (group.panel == nullptr) {
            continue;
        }
        anyGroups = true;
        const bool groupMatch =
            !filtering || wxString::FromUTF8(group.name).Lower().Contains(lowered);
        bool nodeMatch = false;
        for (const std::string& member : group.members) {
            if (filtering && wxString::FromUTF8(member).Lower().Contains(lowered)) {
                nodeMatch = true;
                break;
            }
        }
        const bool visible = groupMatch || nodeMatch;
        group.panel->Show(visible);
        anyVisible = anyVisible || visible;

        for (const std::string& member : group.members) {
            const bool memberVisible =
                !filtering || groupMatch ||
                wxString::FromUTF8(member).Lower().Contains(lowered);
            const auto row = nodeRows_.find(member);
            if (row != nodeRows_.end()) {
                row->second->Show(memberVisible);
            }
        }

        if (group.nodesPanel != nullptr) {
            const bool showNodes =
                collapsed_.count(group.name) == 0 || (filtering && nodeMatch);
            group.nodesPanel->Show(visible && showNodes);
        }
    }

    const bool empty = !anyVisible;
    emptyState_->Show(empty);
    if (empty) {
        emptyState_->SetText(anyGroups ? "No proxy groups or nodes match the search."
                                       : "No proxy groups available. Start the kernel and refresh.");
    }
    RequestRelayout();
}

void ProxiesPage::SelectNode(const std::string& group, const std::string& target) {
    if (pendingGroup_ == group) {
        return;
    }
    pendingGroup_ = group;
    UpdateDynamic(Controller().Snapshot().proxyCatalog);
    Controller().SelectProxy(group, target);
}

std::string ProxiesPage::DelayText(const runtime::ProxyCatalog& catalog,
                                   const std::string& name) const {
    if (catalog.testing.count(name) != 0) {
        return "Testing...";
    }
    const auto delay = catalog.delays.find(name);
    if (delay == catalog.delays.end()) {
        return "";
    }
    if (delay->second < 0) {
        return "Timeout";
    }
    return std::to_string(delay->second) + " ms";
}

}  // namespace clashyuka::ui
