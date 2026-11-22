#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "clashyuka/ui/pages/page_base.h"

class wxBoxSizer;

namespace clashyuka::runtime {
struct ProxyCatalog;
}

namespace clashyuka::ui {

class Button;
class ListItem;
class SearchField;
class TextBlock;

class ProxiesPage final : public PageBase {
public:
    ProxiesPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    struct GroupView {
        std::string name;
        std::string type;
        std::vector<std::string> members;
        wxWindow* panel = nullptr;
        wxWindow* nodesPanel = nullptr;
        ListItem* header = nullptr;
        TextBlock* summary = nullptr;
        Button* testGroup = nullptr;
        Button* unsetFixed = nullptr;
    };

    void Rebuild(const std::shared_ptr<const runtime::ProxyCatalog>& catalog);
    void Clear();
    void UpdateDynamic(const std::shared_ptr<const runtime::ProxyCatalog>& catalog);
    void ApplyFilter();
    void SelectNode(const std::string& group, const std::string& target);
    std::string DelayText(const runtime::ProxyCatalog& catalog,
                          const std::string& name) const;

    SearchField* search_ = nullptr;
    Button* refresh_ = nullptr;
    Button* cancelTests_ = nullptr;
    TextBlock* statusLine_ = nullptr;
    TextBlock* emptyState_ = nullptr;
    wxWindow* groupsHost_ = nullptr;
    wxBoxSizer* groupsSizer_ = nullptr;

    std::vector<GroupView> groups_;
    std::map<std::string, ListItem*> nodeItems_;
    std::map<std::string, wxWindow*> nodeRows_;
    std::set<std::string> expanded_;
    std::string pendingGroup_;
    std::uint64_t builtRevision_ = 0;
    wxString filter_;
};

}  // namespace clashyuka::ui
