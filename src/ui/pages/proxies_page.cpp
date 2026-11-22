#include "clashyuka/ui/pages/proxies_page.h"

#include <wx/sizer.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/ui/components/button.h"
#include "clashyuka/ui/components/list_item.h"
#include "clashyuka/ui/components/search_field.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/pages/page_utils.h"
#include "clashyuka/ui/pages/surface_panel.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui {
namespace {

struct DemoNode {
    wxString name;
    bool selected = false;
};

struct DemoGroup {
    wxString title;
    wxString subtitle;
    std::vector<DemoNode> nodes;
};

}  // namespace

ProxiesPage::ProxiesPage(wxWindow* parent, app::AppController& controller)
    : PageBase(parent, controller, app::PageId::Proxies) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new TextBlock(this,
                             "Demo data: group and node names are placeholders. Live proxy "
                             "groups arrive in P2.3.",
                             TypeStyle::Label, 170),
               0, wxEXPAND);
    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
    modeRow->Add(new TextBlock(this, "Mode", TypeStyle::Label, 170), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(SpaceDp(Space::Md)));
    ruleButton_ = new Button(this, "Rule", ButtonVariant::Text);
    globalButton_ = new Button(this, "Global", ButtonVariant::Text);
    directButton_ = new Button(this, "Direct", ButtonVariant::Text);
    ruleButton_->SetOnClick([this] { Controller().SetProxyMode(app::ProxyMode::Rule); });
    globalButton_->SetOnClick([this] { Controller().SetProxyMode(app::ProxyMode::Global); });
    directButton_->SetOnClick([this] { Controller().SetProxyMode(app::ProxyMode::Direct); });
    for (Button* button : {ruleButton_, globalButton_, directButton_}) {
        modeRow->Add(button, 0, wxRIGHT, FromDIP(SpaceDp(Space::Sm)));
    }
    sizer->Add(modeRow, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    search_ = new SearchField(this, "Search proxy groups");
    search_->SetOnChange([this](const wxString& query) { ApplyFilter(query); });
    sizer->Add(search_, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Lg));

    const std::vector<DemoGroup> groups = {
        {"GLOBAL",
         wxString::FromUTF8("Demo group · routing fallback"),
         {{wxString::FromUTF8("Auto · Hong Kong 01"), true}, {"Hong Kong 02", false},
          {"Japan 01", false}, {"US West 01", false}}},
        {"Streaming",
         wxString::FromUTF8("Demo group · media services"),
         {{"Netflix Singapore", true}, {"Disney Japan", false}}},
    };

    for (const DemoGroup& group : groups) {
        groupTitles_.push_back(group.title);

        auto* block = new SurfacePanel(this);
        auto* blockSizer = new wxBoxSizer(wxVERTICAL);

        auto* header = new ListItem(block);
        header->SetTitle(group.title);
        header->SetSubtitle(group.subtitle);
        header->SetTrailingText(
            wxString::Format("%d nodes", static_cast<int>(group.nodes.size())));
        header->SetLeadingIcon(IconId::Wifi);

        auto* nodes = new SurfacePanel(block);
        auto* nodeSizer = new wxBoxSizer(wxVERTICAL);
        for (const DemoNode& node : group.nodes) {
            auto* item = new ListItem(nodes);
            item->SetTitle(node.name);
            item->SetTrailingText(wxString::FromUTF8("—"));
            item->SetShowChevron(false);
            item->SetSelected(node.selected);
            nodeSizer->Add(item, 0, wxEXPAND | wxLEFT, FromDIP(SpaceDp(Space::Xl)));
        }
        nodes->SetSizer(nodeSizer);

        header->SetOnClick([this, nodes] {
            nodes->Show(!nodes->IsShown());
            RequestRelayout();
        });

        blockSizer->Add(header, 0, wxEXPAND);
        blockSizer->Add(nodes, 0, wxEXPAND);
        block->SetSizer(blockSizer);
        sizer->Add(block, 0, wxEXPAND);
        groupBlocks_.push_back(block);
    }

    emptyFilterHint_ = new TextBlock(this, "No proxy groups match the search.", TypeStyle::Body,
                                     170);
    emptyFilterHint_->Show(false);
    sizer->Add(emptyFilterHint_, 0, wxEXPAND);

    pages::AddSpacer(sizer, this, SpaceDp(Space::Xl));

    auto* note = new TextBlock(this,
                               "Node selection and latency tests are not implemented in this "
                               "build. Selecting a node here would not change the running core.",
                               TypeStyle::Label, 150);
    sizer->Add(note, 0, wxEXPAND);

    SetSizer(sizer);
    SyncState();
}

void ProxiesPage::ApplyState(const app::AppStateSnapshot&) {
    UpdateModeButtons();
}

void ProxiesPage::UpdateModeButtons() {
    const app::ProxyMode mode = Controller().Snapshot().proxyMode;
    const auto variantFor = [](bool active) {
        return active ? ButtonVariant::FilledTonal : ButtonVariant::Text;
    };
    ruleButton_->SetVariant(variantFor(mode == app::ProxyMode::Rule));
    globalButton_->SetVariant(variantFor(mode == app::ProxyMode::Global));
    directButton_->SetVariant(variantFor(mode == app::ProxyMode::Direct));
}

void ProxiesPage::ApplyFilter(const wxString& query) {
    const wxString lowered = query.Lower().Trim();
    bool anyVisible = false;
    for (std::size_t i = 0; i < groupBlocks_.size() && i < groupTitles_.size(); ++i) {
        const bool visible = lowered.empty() || groupTitles_[i].Lower().Contains(lowered);
        groupBlocks_[i]->Show(visible);
        anyVisible = anyVisible || visible;
    }
    if (emptyFilterHint_ != nullptr) {
        emptyFilterHint_->Show(!anyVisible);
    }
    RequestRelayout();
}

}  // namespace clashyuka::ui
