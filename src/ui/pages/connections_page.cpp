#include "clashyuka/ui/pages/connections_page.h"

#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {

ConnectionsPage::ConnectionsPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Connections) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             "No connection data source is connected yet. The list, filters "
                             "and search are ready for the Mihomo integration.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* search = new SearchField(this, "Search connections");
    toolbar->Add(search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    allButton_ = new Button(this, "All", ButtonVariant::FilledTonal);
    tcpButton_ = new Button(this, "TCP", ButtonVariant::Text);
    udpButton_ = new Button(this, "UDP", ButtonVariant::Text);
    allButton_->SetOnClick([this] {
        filter_ = Filter::All;
        UpdateFilters();
    });
    tcpButton_->SetOnClick([this] {
        filter_ = Filter::Tcp;
        UpdateFilters();
    });
    udpButton_->SetOnClick([this] {
        filter_ = Filter::Udp;
        UpdateFilters();
    });
    for (Button* button : {allButton_, tcpButton_, udpButton_}) {
        toolbar->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Xs)));
    }
    sizer->Add(toolbar, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* emptyCard = pages::MakeCard(this);
    auto* emptySizer = new wxBoxSizer(wxVERTICAL);
    emptySizer->Add(new TextBlock(emptyCard, "No active connections", TypeStyle::Title), 0,
                    wxEXPAND);
    emptySizer->Add(new TextBlock(emptyCard,
                                  "Connections will appear here once Mihomo is running and "
                                  "the API stream is connected.",
                                  TypeStyle::Body, 190),
                    0, wxEXPAND);
    pages::AddSpacer(emptySizer, emptyCard, SpaceDp(Space::Md));
    auto* countRow = new wxBoxSizer(wxHORIZONTAL);
    countRow->Add(new TextBlock(emptyCard, "Active", TypeStyle::Label, 170), 0,
                  wxALIGN_CENTER_VERTICAL);
    countRow->AddStretchSpacer();
    countValue_ = new TextBlock(emptyCard, "0", TypeStyle::Body);
    countRow->Add(countValue_, 0, wxALIGN_CENTER_VERTICAL);
    emptySizer->Add(countRow, 0, wxEXPAND);
    emptyCard->SetContent(emptySizer, 24);
    sizer->Add(emptyCard, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* details = new TextBlock(this, "Connection details", TypeStyle::Title);
    sizer->Add(details, 0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Sm));

    auto* detailsItem = new ListItem(this);
    detailsItem->SetTitle("Connection inspector");
    detailsItem->SetSubtitle("Host, process, rule and chain will be shown here.");
    detailsItem->SetTrailingText("Not available");
    detailsItem->SetLeadingIcon(IconId::Info);
    detailsItem->SetShowChevron(false);
    detailsItem->Enable(false);
    sizer->Add(detailsItem, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void ConnectionsPage::ApplyState(const app::AppStateSnapshot& state) {
    if (countValue_ != nullptr) {
        countValue_->SetText(wxString::Format("%d", state.connectionCount));
    }
}

void ConnectionsPage::UpdateFilters() {
    const auto variantFor = [](bool active) {
        return active ? ButtonVariant::FilledTonal : ButtonVariant::Text;
    };
    allButton_->SetVariant(variantFor(filter_ == Filter::All));
    tcpButton_->SetVariant(variantFor(filter_ == Filter::Tcp));
    udpButton_->SetVariant(variantFor(filter_ == Filter::Udp));
}

}  // namespace clashyuka::ui
