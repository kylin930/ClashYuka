#include "clashyuka/app/page_registry.h"

#include "clashyuka/ui/pages/connections_page.h"
#include "clashyuka/ui/pages/logs_page.h"
#include "clashyuka/ui/pages/overview_page.h"
#include "clashyuka/ui/pages/profiles_page.h"
#include "clashyuka/ui/pages/proxies_page.h"
#include "clashyuka/ui/pages/rules_page.h"
#include "clashyuka/ui/pages/settings_page.h"

namespace clashyuka::app {
namespace {

template <typename Page>
ui::PageBase* CreatePage(wxWindow* parent, AppController& controller) {
    return new Page(parent, controller);
}

const std::vector<PageDescriptor>& Descriptors() {
    static const std::vector<PageDescriptor> pages = {
        {PageId::Overview, "Dashboard", "Core status, profile and traffic at a glance",
         ui::IconId::Dashboard, &CreatePage<ui::OverviewPage>},
        {PageId::Proxies, "Proxies", "Proxy groups and node selection", ui::IconId::Wifi,
         &CreatePage<ui::ProxiesPage>},
        {PageId::Connections, "Connections", "Live connections from Mihomo",
         ui::IconId::Activity, &CreatePage<ui::ConnectionsPage>},
        {PageId::Rules, "Rules", "Active routing rules and rule providers", ui::IconId::Check,
         &CreatePage<ui::RulesPage>},
        {PageId::Profiles, "Profiles", "Configuration files and subscriptions",
         ui::IconId::Home, &CreatePage<ui::ProfilesPage>},
        {PageId::Logs, "Logs", "Kernel and connection logs", ui::IconId::Menu,
         &CreatePage<ui::LogsPage>},
        {PageId::Settings, "Settings", "Appearance, general and Mihomo options",
         ui::IconId::Settings, &CreatePage<ui::SettingsPage>},
    };
    return pages;
}

}  // namespace

const std::vector<PageDescriptor>& PageRegistry::All() {
    return Descriptors();
}

const PageDescriptor& PageRegistry::Get(PageId id) {
    for (const PageDescriptor& page : Descriptors()) {
        if (page.id == id) {
            return page;
        }
    }
    return Descriptors().front();
}

int PageRegistry::IndexOf(PageId id) {
    const auto& pages = Descriptors();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char* PageIdName(PageId id) {
    switch (id) {
        case PageId::Overview:
            return "overview";
        case PageId::Proxies:
            return "proxies";
        case PageId::Connections:
            return "connections";
        case PageId::Rules:
            return "rules";
        case PageId::Profiles:
            return "profiles";
        case PageId::Logs:
            return "logs";
        case PageId::Settings:
            return "settings";
    }
    return "unknown";
}

}  // namespace clashyuka::app
