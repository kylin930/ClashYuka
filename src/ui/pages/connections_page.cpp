#include "clashyuka/ui/pages/connections_page.h"

#include <algorithm>
#include <array>
#include <utility>

#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

wxString Endpoint(const std::string& address, int port) {
    if (address.empty()) {
        return {};
    }
    auto text = wxString::FromUTF8(address);
    if (port > 0) {
        if (text.Contains(":") && !text.StartsWith("[")) {
            text = "[" + text + "]";
        }
        text += wxString::Format(":%d", port);
    }
    return text;
}

wxString Chain(const std::vector<std::string>& hops) {
    wxString text;
    for (const auto& hop : hops) {
        if (!text.empty()) {
            text += " / ";
        }
        text += wxString::FromUTF8(hop);
    }
    return text;
}

wxString Rule(const mihomo::ConnectionInfo& row) {
    auto text = wxString::FromUTF8(row.rule);
    if (!row.rulePayload.empty()) {
        if (!text.empty()) {
            text += ": ";
        }
        text += wxString::FromUTF8(row.rulePayload);
    }
    return text;
}

wxString TrafficBytes(std::uint64_t bytes) {
    constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    if (bytes < 1024) {
        return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
    }
    double size = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (size >= 1024 && unit + 1 < std::size(units)) {
        size /= 1024;
        ++unit;
    }
    return wxString::Format("%.1f %s", size, wxString::FromUTF8(units[unit]));
}

struct ConnectionRow {
    std::string id;
    std::array<wxString, 5> columns;
    bool operator==(const ConnectionRow&) const = default;
};

}  // namespace

class ConnectionList final : public wxListCtrl {
public:
    explicit ConnectionList(wxWindow* parent)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(180, 280)),
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL) {
        AppendColumn("Target", wxLIST_FORMAT_LEFT, FromDIP(210));
        AppendColumn("Protocol", wxLIST_FORMAT_LEFT, FromDIP(75));
        AppendColumn("Proxy chain", wxLIST_FORMAT_LEFT, FromDIP(175));
        AppendColumn("Upload", wxLIST_FORMAT_RIGHT, FromDIP(85));
        AppendColumn("Download", wxLIST_FORMAT_RIGHT, FromDIP(85));
        SetName("Active connections");
    }

    const std::string& IdAt(long index) const { return rows_[index].id; }

    void Replace(std::vector<ConnectionRow> rows, const std::string& selectedId) {
        if (rows_ == rows) {
            return;
        }
        wxWindowUpdateLocker lock(this);
        const auto oldSelected = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        const auto selected = selectedId.empty() ? rows.end()
            : std::ranges::find(rows, selectedId, &ConnectionRow::id);
        const auto index = selected == rows.end() ? -1L : static_cast<long>(selected - rows.begin());
        if (oldSelected >= 0 && oldSelected != index) {
            SetItemState(oldSelected, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        }
        const auto oldCount = rows_.size();
        rows_ = std::move(rows);
        if (oldCount != rows_.size()) {
            SetItemCount(static_cast<long>(rows_.size()));
        }
        if (index >= 0 && (GetItemState(index, wxLIST_STATE_SELECTED) == 0 || oldSelected != index)) {
            SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                         wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        }
        const auto top = GetTopItem();
        if (!rows_.empty() && top >= 0 && top < GetItemCount()) {
            RefreshItems(top, std::min<long>(GetItemCount() - 1, top + GetCountPerPage()));
        }
    }

private:
    wxString OnGetItemText(long item, long column) const override {
        return item >= 0 && static_cast<std::size_t>(item) < rows_.size() &&
                       column >= 0 && column < 5
                   ? rows_[item].columns[column] : wxString{};
    }

    std::vector<ConnectionRow> rows_;
};

ConnectionsPage::ConnectionsPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Connections) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    sizer->Add(status_, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* search = new SearchField(this, "Search endpoints, rules, or proxy chains");
    search->SetOnChange([this](const wxString& query) {
        query_ = query.Lower().Trim().Trim(false);
        RefreshRows();
    });
    toolbar->Add(search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    allButton_ = new Button(this, "All", ButtonVariant::FilledTonal);
    tcpButton_ = new Button(this, "TCP", ButtonVariant::Text);
    udpButton_ = new Button(this, "UDP", ButtonVariant::Text);
    allButton_->SetOnClick([this] { SetFilter(Filter::All); });
    tcpButton_->SetOnClick([this] { SetFilter(Filter::Tcp); });
    udpButton_->SetOnClick([this] { SetFilter(Filter::Udp); });
    for (Button* button : {allButton_, tcpButton_, udpButton_}) {
        toolbar->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    }
    sizer->Add(toolbar, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    list_ = new ConnectionList(this);
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        if (!updatingList_ && event.GetIndex() >= 0 && event.GetIndex() < list_->GetItemCount()) {
            selectedId_ = list_->IdAt(event.GetIndex());
            RefreshDetails();
        }
    });
    list_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) {
        if (!updatingList_ && list_->GetSelectedItemCount() == 0) {
            selectedId_.clear();
            RefreshDetails();
        }
    });
    sizer->Add(list_, 1, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Md));

    auto* detailsHeader = new wxBoxSizer(wxHORIZONTAL);
    detailsHeader->Add(new TextBlock(this, "Connection details", TypeStyle::Title), 1,
                       wxALIGN_CENTER_VERTICAL);
    closeButton_ = new Button(this, "Close connection", ButtonVariant::FilledTonal);
    closeButton_->Enable(false);
    closeButton_->SetOnClick([this] { Controller().CloseConnection(live_.generation, selectedId_); });
    detailsHeader->Add(closeButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(detailsHeader, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));
    details_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              FromDIP(wxSize(160, 140)), wxTE_MULTILINE | wxTE_READONLY);
    details_->SetName("Connection details");
    sizer->Add(details_, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
    RefreshRows();
}

void ConnectionsPage::ApplyState(const app::AppStateSnapshot& state) {
    const auto& next = state.live;
    const bool newGeneration = live_.generation != next.generation;
    const bool rowsChanged = newGeneration || live_.connections != next.connections ||
        live_.running != next.running || live_.connectionsStatus != next.connectionsStatus ||
        live_.connectionsFresh != next.connectionsFresh;
    const bool actionChanged = live_.switching != next.switching ||
        live_.pendingClose != next.pendingClose;
    if (!rowsChanged && !actionChanged) {
        return;
    }
    if (newGeneration) {
        selectedId_.clear();
    }
    live_ = next;
    live_.logs.reset();
    if (rowsChanged) {
        RefreshRows();
    } else {
        RefreshDetails();
    }
}

void ConnectionsPage::SetFilter(Filter filter) {
    if (filter_ == filter) {
        return;
    }
    filter_ = filter;
    const auto variantFor = [](bool active) {
        return active ? ButtonVariant::FilledTonal : ButtonVariant::Text;
    };
    allButton_->SetVariant(variantFor(filter_ == Filter::All));
    tcpButton_->SetVariant(variantFor(filter_ == Filter::Tcp));
    udpButton_->SetVariant(variantFor(filter_ == Filter::Udp));
    RefreshRows();
}

void ConnectionsPage::RefreshRows() {
    std::vector<ConnectionRow> rows;
    const auto matches = [&](const wxString& text) { return text.Lower().Contains(query_); };
    if (live_.connections) {
        rows.reserve(live_.connections->connections.size());
        bool selectionExists = selectedId_.empty();
        for (const auto& row : live_.connections->connections) {
            selectionExists |= row.id == selectedId_;
            const auto& m = row.metadata;
            const auto protocol = wxString::FromUTF8(m.network).Upper();
            if ((filter_ == Filter::Tcp && protocol != "TCP") ||
                (filter_ == Filter::Udp && protocol != "UDP")) {
                continue;
            }
            const auto target = Endpoint(m.host.empty() ? m.destinationIP : m.host, m.destinationPort);
            const auto chain = Chain(row.chains);
            if (!query_.empty() && !matches(target) && !matches(chain) && !matches(Rule(row)) &&
                !matches(Endpoint(m.sourceIP, m.sourcePort)) &&
                !matches(Endpoint(m.destinationIP, m.destinationPort)) &&
                !matches(Chain(row.providerChains))) {
                continue;
            }
            rows.push_back({row.id, {target.empty() ? wxString("Unknown") : target,
                                    protocol, chain, TrafficBytes(row.upload), TrafficBytes(row.download)}});
        }
        if (!selectionExists) {
            selectedId_.clear();
        }
    } else {
        selectedId_.clear();
    }

    const auto count = live_.connections ? live_.connections->connections.size() : 0;
    wxString status;
    if (!live_.running) {
        status = "Mihomo is not running.";
    } else if (live_.connectionsStatus == runtime::StreamAvailability::Disabled) {
        status = "Mihomo connection stream is disabled.";
    } else if (live_.connectionsStatus == runtime::StreamAvailability::Disconnected) {
        status = live_.connections ? wxString::Format("Connection stream interrupted. Last seen: %zu.", count)
                                   : wxString("Connection stream interrupted.");
    } else if (!live_.connectionsFresh) {
        status = "Waiting for live connection data.";
    } else if (count == 0) {
        status = "No active connections.";
    } else if (rows.empty()) {
        status = wxString::Format("No matches among %zu active connections.", count);
    } else {
        status = wxString::Format("Showing %zu of %zu active connections.", rows.size(), count);
    }
    if (status_->GetLabel() != status) {
        status_->SetLabel(status);
    }
    updatingList_ = true;
    list_->Replace(std::move(rows), selectedId_);
    updatingList_ = false;
    RefreshDetails();
}

void ConnectionsPage::RefreshDetails() {
    const auto* selected = static_cast<const mihomo::ConnectionInfo*>(nullptr);
    if (live_.connections && !selectedId_.empty()) {
        const auto& rows = live_.connections->connections;
        const auto row = std::ranges::find(rows, selectedId_, &mihomo::ConnectionInfo::id);
        if (row != rows.end()) {
            selected = &*row;
        }
    }
    wxString text;
    if (selected) {
        const auto add = [&](const wxString& label, const wxString& value) {
            if (!value.empty()) {
                if (!text.empty()) {
                    text += "\n";
                }
                text += label + ": " + value;
            }
        };
        const auto& m = selected->metadata;
        add("Host", wxString::FromUTF8(m.host));
        add("Destination", Endpoint(m.destinationIP, m.destinationPort));
        add("Source", Endpoint(m.sourceIP, m.sourcePort));
        add("Protocol", wxString::FromUTF8(m.network).Upper());
        add("Inbound type", wxString::FromUTF8(m.type));
        add("Process", wxString::FromUTF8(m.processPath));
        add("Rule", Rule(*selected));
        add("Proxy chain", Chain(selected->chains));
        add("Providers", Chain(selected->providerChains));
        add("DNS mode", wxString::FromUTF8(m.dnsMode));
    }
    if (text.empty()) {
        text = selected ? "No details were reported for this connection."
                        : "Select a connection to view its route.";
    }
    if (live_.pendingClose && live_.pendingClose->connectionId == selectedId_) {
        text = (live_.pendingClose->awaitingSnapshot ? "Waiting for updated connection data...\n"
                                                     : "Closing connection...\n") + text;
    }
    if (details_->GetValue() != text) {
        details_->ChangeValue(text);
    }
    const bool canClose = live_.CanClose(live_.generation, selectedId_) &&
                          list_->GetSelectedItemCount() != 0;
    if (closeButton_->IsThisEnabled() != canClose) {
        closeButton_->Enable(canClose);
    }
}

}  // namespace clashyuka::ui
