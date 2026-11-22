#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include <wx/string.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/app/app_state.h"
#include "clashyuka/app/page_registry.h"
#include "clashyuka/ui/theme/theme_manager.h"

using namespace clashyuka;
using namespace clashyuka::app;

namespace {

struct RecordingObserver final : AppStateObserver {
    int calls = 0;
    AppStateSnapshot last;

    void OnAppStateChanged(const AppStateSnapshot& state) override {
        ++calls;
        last = state;
    }
};

class MockShell final : public AppShell {
public:
    int messages = 0;
    int dialogs = 0;
    wxString lastTitle;
    wxString lastBody;

    void ShowMessage(const wxString&) override { ++messages; }
    void ShowInfoDialog(const wxString& title, const wxString& body) override {
        ++dialogs;
        lastTitle = title;
        lastBody = body;
    }
};

}  // namespace

TEST_CASE("app state starts in the disconnected shell state") {
    AppState state;
    const AppStateSnapshot& snapshot = state.Get();
    CHECK(snapshot.kernel == KernelState::NotStarted);
    CHECK(snapshot.api == ApiState::Disconnected);
    CHECK(snapshot.currentPage == PageId::Overview);
    CHECK(snapshot.theme == ui::ColorTheme::Purple);
    CHECK_FALSE(snapshot.dark);
    CHECK(snapshot.animationsEnabled);
    CHECK_FALSE(snapshot.reducedMotion);
    CHECK(snapshot.proxyMode == ProxyMode::Rule);
    CHECK_FALSE(snapshot.profileLoaded);
    CHECK_FALSE(snapshot.trafficAvailable);
    CHECK(snapshot.connectionCount == 0);
}

TEST_CASE("app state notifies observers and skips unchanged values") {
    AppState state;
    RecordingObserver observer;
    state.AddObserver(&observer);

    state.SetKernelState(KernelState::Starting);
    CHECK(observer.calls == 1);
    CHECK(observer.last.kernel == KernelState::Starting);

    state.SetKernelState(KernelState::Starting);
    CHECK(observer.calls == 1);

    state.SetApiState(ApiState::Connecting);
    state.SetProxyMode(ProxyMode::Global);
    state.SetProfile("demo.yaml", true);
    state.SetTraffic(1024.0, 2048.0, true);
    CHECK(observer.calls == 5);
    CHECK(observer.last.api == ApiState::Connecting);
    CHECK(observer.last.proxyMode == ProxyMode::Global);
    CHECK(observer.last.profileName == "demo.yaml");
    CHECK(observer.last.trafficAvailable);
}

TEST_CASE("app state supports several observers and removal") {
    AppState state;
    RecordingObserver first;
    RecordingObserver second;
    state.AddObserver(&first);
    state.AddObserver(&second);
    state.AddObserver(&first);

    state.SetConnectionCount(3);
    CHECK(first.calls == 1);
    CHECK(second.calls == 1);

    state.RemoveObserver(&second);
    state.SetConnectionCount(4);
    CHECK(first.calls == 2);
    CHECK(second.calls == 1);
}

TEST_CASE("observers can subscribe or unsubscribe during a notification") {
    struct AddingObserver final : AppStateObserver {
        AppState* state = nullptr;
        AppStateObserver* extra = nullptr;
        int calls = 0;

        void OnAppStateChanged(const AppStateSnapshot&) override {
            ++calls;
            if (state != nullptr && extra != nullptr) {
                state->AddObserver(extra);
                extra = nullptr;
            }
        }
    };
    struct SelfRemovingObserver final : AppStateObserver {
        AppState* state = nullptr;
        int calls = 0;

        void OnAppStateChanged(const AppStateSnapshot&) override {
            ++calls;
            state->RemoveObserver(this);
        }
    };

    AppState state;
    RecordingObserver late;
    AddingObserver adding;
    adding.state = &state;
    adding.extra = &late;
    state.AddObserver(&adding);

    state.SetConnectionCount(1);
    CHECK(adding.calls == 1);
    CHECK(late.calls == 0);

    state.SetConnectionCount(2);
    CHECK(adding.calls == 2);
    CHECK(late.calls == 1);

    SelfRemovingObserver removing;
    removing.state = &state;
    state.AddObserver(&removing);
    state.SetConnectionCount(3);
    state.SetConnectionCount(4);
    CHECK(removing.calls == 1);
}

TEST_CASE("controller navigates and publishes the current page") {
    MockShell shell;
    AppController controller(&shell);
    RecordingObserver observer;
    controller.State().AddObserver(&observer);

    controller.Navigate(PageId::Logs);
    CHECK(controller.Snapshot().currentPage == PageId::Logs);
    CHECK(observer.calls == 1);

    controller.Navigate(PageId::Logs);
    CHECK(observer.calls == 1);
}

TEST_CASE("controller keeps theme manager and state in sync") {
    ui::ThemeManager& theme = ui::ThemeManager::Instance();
    const ui::ColorTheme originalTheme = theme.GetColorTheme();
    const bool originalDark = theme.IsDark();
    const bool originalAnimations = theme.AnimationsEnabled();
    const bool originalReduced = theme.ReducedMotion();

    MockShell shell;
    AppController controller(&shell);

    controller.SetColorTheme(ui::ColorTheme::Teal);
    CHECK(theme.GetColorTheme() == ui::ColorTheme::Teal);
    CHECK(controller.Snapshot().theme == ui::ColorTheme::Teal);

    controller.SetDarkMode(true);
    CHECK(theme.IsDark());
    CHECK(controller.Snapshot().dark);

    controller.SetAnimationsEnabled(false);
    CHECK_FALSE(theme.AnimationsEnabled());
    CHECK_FALSE(controller.Snapshot().animationsEnabled);

    controller.SetReducedMotion(true);
    CHECK(theme.ReducedMotion());
    CHECK(controller.Snapshot().reducedMotion);

    controller.SetProxyMode(ProxyMode::Direct);
    CHECK(controller.Snapshot().proxyMode == ProxyMode::Direct);

    theme.SetColorTheme(originalTheme);
    theme.SetDarkMode(originalDark);
    theme.SetAnimationsEnabled(originalAnimations);
    theme.SetReducedMotion(originalReduced);
}

TEST_CASE("controller forwards shell messages") {
    MockShell shell;
    AppController controller(&shell);

    controller.ShowMessage("Saved");
    CHECK(shell.messages == 1);
    CHECK(shell.dialogs == 0);

    controller.ShowNotImplemented("Node switching", "This is a placeholder.");
    CHECK(shell.dialogs == 1);
    CHECK(shell.lastTitle == "Node switching");
    CHECK(shell.lastBody.Contains("not available"));
    CHECK(shell.lastBody.Contains("No action was performed"));
}

TEST_CASE("page registry exposes the shell pages in navigation order") {
    const auto& pages = PageRegistry::All();
    REQUIRE(pages.size() == 6);

    const PageId expected[] = {PageId::Overview, PageId::Proxies, PageId::Connections,
                               PageId::Profiles, PageId::Logs, PageId::Settings};
    for (std::size_t i = 0; i < pages.size(); ++i) {
        CHECK(pages[i].id == expected[i]);
        CHECK(PageRegistry::IndexOf(expected[i]) == static_cast<int>(i));
        CHECK_FALSE(pages[i].title.empty());
        CHECK_FALSE(pages[i].description.empty());
        CHECK(pages[i].create != nullptr);
    }

    CHECK(PageRegistry::Get(PageId::Settings).title == "Settings");
    CHECK(PageRegistry::IndexOf(PageId::Settings) == 5);
    CHECK(std::string(PageIdName(PageId::Proxies)) == "proxies");
    CHECK(std::string(PageIdName(PageId::Settings)) == "settings");
}
