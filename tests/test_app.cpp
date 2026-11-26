#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include <wx/string.h>

#include "clashyuka/app/app_controller.h"
#include "clashyuka/app/app_state.h"
#include "clashyuka/app/app_text.h"
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

TEST_CASE("kernel error text distinguishes preflight rejection from config preparation failure") {
    CHECK(KernelErrorText(core::ErrorKind::ProfilePreflightRejected) ==
          "Mihomo rejected the profile configuration. Check the profile or select another one.");
    CHECK(KernelErrorText(core::ErrorKind::ConfigFailed) ==
          "The runtime configuration could not be prepared. Check the settings and try again.");
}

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
    CHECK_FALSE(snapshot.profiles);
    CHECK_FALSE(snapshot.trafficAvailable);
    CHECK(snapshot.connectionCount == 0);
}

TEST_CASE("live logs cap bursts and retain only the stopped generation until the next start") {
    AppState state;
    RecordingObserver observer;
    state.AddObserver(&observer);
    runtime::KernelSnapshot kernel;
    kernel.generation = 1;
    kernel.phase = runtime::KernelPhase::Running;
    kernel.logStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);

    std::vector<mihomo::MihomoLogEntry> batch;
    for (int i = 0; i < 505; ++i) {
        batch.push_back({"info", std::to_string(i)});
    }
    const int calls = observer.calls;
    state.AppendLogs(1, batch);
    REQUIRE(state.Get().live.logs);
    CHECK(observer.calls == calls + 1);
    CHECK(state.Get().live.logs->size() == 500);
    CHECK(state.Get().live.logs->front().payload == "5");
    state.AppendLogs(1, {{"info", "latest"}});
    CHECK(state.Get().live.logs->front().payload == "6");
    CHECK(state.Get().live.logs->back().payload == "latest");
    state.ClearLogs();
    CHECK_FALSE(state.Get().live.logs);
    CHECK(state.Get().live.logsStatus == runtime::StreamAvailability::Available);
    state.AppendLogs(1, {{"info", "after clear"}});

    kernel.phase = runtime::KernelPhase::Stopped;
    state.UpdateLiveStreams(kernel);
    state.AppendLogs(1, {{"error", "late"}});
    REQUIRE(state.Get().live.logs);
    CHECK(state.Get().live.logs->size() == 1);
    CHECK(state.Get().live.logs->back().payload == "after clear");
    kernel.generation = 2;
    kernel.phase = runtime::KernelPhase::Starting;
    state.UpdateLiveStreams(kernel);
    CHECK_FALSE(state.Get().live.logs);
    kernel.phase = runtime::KernelPhase::Running;
    state.UpdateLiveStreams(kernel);
    state.AppendLogs(1, {{"error", "previous generation"}});
    CHECK_FALSE(state.Get().live.logs);
}

TEST_CASE("live data scrubs secrets and remembers edited subscription URLs") {
    AppState state;
    runtime::KernelSnapshot kernel;
    kernel.phase = runtime::KernelPhase::Running;
    kernel.generation = 1;
    kernel.connectionStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);
    const std::string oldUrl = "https://example.test/sub?token=old";
    const std::string newUrl = "https://example.test/sub?token=new";
    CHECK(state.RememberRedaction("controller-secret"));
    CHECK(state.RememberRedaction(oldUrl));
    state.AppendLogs(1, {{"controller-secret", "before " + oldUrl}, {"warn", newUrl},
                        {"info", std::string(4090, 'x') + newUrl}});
    CHECK(state.RememberRedaction(newUrl));
    CHECK_FALSE(state.RememberRedaction(newUrl));
    REQUIRE(state.Get().live.logs);
    REQUIRE(state.Get().live.logs->size() == 2);
    CHECK(state.Get().live.logs->front().type == "[redacted]");
    CHECK(state.Get().live.logs->front().payload == "before [redacted]");
    CHECK(state.Get().live.logs->back().payload == "[redacted]");
    state.AppendLogs(1, {{"info", oldUrl + " " + newUrl},
                        {"info", std::string(4090, 'a') + "controller-secret"}});
    CHECK((*state.Get().live.logs)[2].payload == "[redacted] [redacted]");
    CHECK(state.Get().live.logs->back().payload.size() <= 4096);
    CHECK(state.Get().live.logs->back().payload.find("control") == std::string::npos);

    mihomo::ConnectionsSnapshot connections;
    connections.connections.emplace_back();
    connections.connections.back().id = "connection-id";
    connections.connections.back().metadata.host = newUrl;
    connections.connections.back().chains = {"controller-secret", oldUrl};
    state.PublishConnections(1, connections);
    REQUIRE(state.Get().live.connections);
    const auto& row = state.Get().live.connections->connections.front();
    CHECK(row.id == "connection-id");
    CHECK(row.metadata.host == "[redacted]");
    CHECK(row.chains == std::vector<std::string>{"[redacted]", "[redacted]"});
}

TEST_CASE("application diagnostics stay readable while the kernel is stopped") {
    AppState state;
    CHECK_FALSE(state.Get().live.logs);
    CHECK(state.RememberRedaction("https://example.test/sub?token=hidden"));
    state.AppendAppLog("error", "Profile rejected: failed to fetch https://example.test/sub?token=hidden");
    REQUIRE(state.Get().live.logs);
    CHECK(state.Get().live.logs->front().type == "error");
    CHECK(state.Get().live.logs->front().payload == "Profile rejected: failed to fetch [redacted]");

    runtime::KernelSnapshot kernel;
    kernel.generation = 1;
    kernel.phase = runtime::KernelPhase::Failed;
    state.UpdateLiveStreams(kernel);
    state.AppendAppLog("error", "Mihomo failed to start.");
    REQUIRE(state.Get().live.logs);
    CHECK(state.Get().live.logs->back().payload == "Mihomo failed to start.");

    kernel.generation = 2;
    kernel.phase = runtime::KernelPhase::Starting;
    state.UpdateLiveStreams(kernel);
    CHECK_FALSE(state.Get().live.logs);
}

TEST_CASE("live connection snapshots and close results are scoped to current requests") {
    AppState state;
    runtime::KernelSnapshot kernel;
    kernel.phase = runtime::KernelPhase::Running;
    kernel.generation = 1;
    kernel.connectionStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);
    mihomo::ConnectionsSnapshot rows;
    rows.connections.emplace_back().id = "id";
    state.PublishConnections(1, rows);
    CHECK(state.Get().connectionCount == 1);
    CHECK(state.Get().live.CanClose(1, "id"));
    CHECK_FALSE(state.Get().live.CanClose(1, "absent"));
    CHECK_FALSE(state.Get().live.CanClose(2, "id"));
    CHECK(state.BeginConnectionClose({10, 1, "id"}));
    CHECK_FALSE(state.BeginConnectionClose({11, 1, "id"}));
    CHECK_FALSE(state.FinishConnectionClose({11, 1, "id", {}}));
    CHECK_FALSE(state.FinishConnectionClose({10, 1, "other", {}}));
    const auto beforeClose = state.Get().live.connections;
    CHECK(state.FinishConnectionClose({10, 1, "id", {}}));
    CHECK_FALSE(state.FinishConnectionClose({10, 1, "id", {}}));
    CHECK_FALSE(state.BeginConnectionClose({11, 1, "id"}));
    CHECK(state.Get().live.connections == beforeClose);
    CHECK(state.Get().connectionCount == 1);

    kernel.profileSwitchInProgress = true;
    state.UpdateLiveStreams(kernel);
    CHECK_FALSE(state.Get().live.CanClose(1, "id"));
    kernel.profileSwitchInProgress = false;
    kernel.connectionStream.availability = runtime::StreamAvailability::Disconnected;
    state.UpdateLiveStreams(kernel);
    CHECK_FALSE(state.Get().live.CanClose(1, "id"));
    kernel.connectionStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);
    CHECK_FALSE(state.Get().live.CanClose(1, "id"));
    state.PublishConnections(1, {});
    REQUIRE(state.Get().live.connections);
    CHECK(state.Get().connectionCount == 0);
    CHECK_FALSE(state.Get().live.CanClose(1, "id"));
    state.PublishConnections(1, rows);
    CHECK(state.BeginConnectionClose({12, 1, "id"}));

    kernel.phase = runtime::KernelPhase::Stopping;
    state.UpdateLiveStreams(kernel);
    state.PublishConnections(1, rows);
    CHECK(state.Get().connectionCount == 0);
    CHECK_FALSE(state.Get().live.connections);
    kernel.generation = 2;
    kernel.phase = runtime::KernelPhase::Running;
    state.UpdateLiveStreams(kernel);
    state.PublishConnections(1, rows);
    CHECK_FALSE(state.Get().live.connections);
    state.PublishConnections(2, rows);
    CHECK(state.BeginConnectionClose({13, 2, "id"}));
    CHECK_FALSE(state.FinishConnectionClose({12, 1, "id", {}}));
    REQUIRE(state.Get().live.pendingClose);
    CHECK(state.Get().live.pendingClose->requestId == 13);
    CHECK(state.FinishConnectionClose({13, 2, "id", core::MakeError(core::ErrorKind::ConnectionFailed,
                                                                  "untrusted response body")}));
    CHECK(state.Get().live.CanClose(2, "id"));
    CHECK(state.Get().connectionCount == 1);
}

TEST_CASE("connection availability is independent and replaces only with current valid snapshots") {
    AppState state;
    runtime::KernelSnapshot kernel;
    kernel.phase = runtime::KernelPhase::Running;
    kernel.generation = 3;
    kernel.connectionStream.availability = runtime::StreamAvailability::Available;
    kernel.logStream.availability = runtime::StreamAvailability::Disconnected;
    state.UpdateLiveStreams(kernel);

    mihomo::ConnectionsSnapshot rows;
    rows.connections.emplace_back().id = "stable-id";
    rows.connections.front().upload = 7;
    state.PublishConnections(3, rows);
    const auto first = state.Get().live.connections;
    REQUIRE(first);
    rows.connections.front().upload = 9;
    kernel.connectionStream.availability = runtime::StreamAvailability::Disconnected;
    kernel.logStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);
    state.PublishConnections(3, rows);
    CHECK(state.Get().live.connections == first);
    CHECK_FALSE(state.Get().live.connectionsFresh);
    CHECK(state.Get().live.logsStatus == runtime::StreamAvailability::Available);

    kernel.connectionStream.availability = runtime::StreamAvailability::Available;
    state.UpdateLiveStreams(kernel);
    state.PublishConnections(2, {});
    CHECK(state.Get().live.connections == first);
    state.PublishConnections(3, rows);
    REQUIRE(state.Get().live.connections != first);
    CHECK(state.Get().live.connections->connections.front().id == "stable-id");
    CHECK(state.Get().live.connections->connections.front().upload == 9);
    CHECK(first->connections.front().upload == 7);
}

TEST_CASE("system proxy retry is unavailable after an ordinary enable failure") {
    AppState state;
    state.SetSystemProxy(true, false, "", "Failure: enable failed", false);
    CHECK_FALSE(state.Get().systemProxyRetryAvailable);
}

TEST_CASE("system proxy recovery failure remains visible when preference is disabled") {
    AppState state;
    state.SetSystemProxy(false, false, "", "Recovery pending or failure", true);
    CHECK_FALSE(state.Get().systemProxyEnabled);
    CHECK_FALSE(state.Get().systemProxyActive);
    CHECK(state.Get().systemProxyAddress.empty());
    CHECK(state.Get().systemProxyStatus == "Recovery pending or failure");
    CHECK(state.Get().systemProxyRetryAvailable);
    state.SetSystemProxy(false, false, "", "Off", false);
    CHECK_FALSE(state.Get().systemProxyRetryAvailable);
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
    ProfilesSnapshot profiles;
    profiles.selectedProfileId = "builtin";
    profiles.runningProfileId = "local-id";
    state.SetProfiles(profiles);
    state.SetProfiles(profiles);
    state.SetTraffic(1024.0, 2048.0, true);
    CHECK(observer.calls == 5);
    CHECK(observer.last.api == ApiState::Connecting);
    CHECK(observer.last.proxyMode == ProxyMode::Global);
    REQUIRE(observer.last.profiles);
    CHECK(observer.last.profiles->selectedProfileId == "builtin");
    CHECK(observer.last.profiles->runningProfileId == "local-id");
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

TEST_CASE("rules state distinguishes an empty response from unavailable data") {
    AppState state;
    RecordingObserver observer;
    state.AddObserver(&observer);
    CHECK_FALSE(state.Get().rulesAvailable);
    runtime::RulesSnapshot ready;
    ready.rules = std::make_shared<const mihomo::RuleCollection>();
    ready.rulesStatus = runtime::RulesLoadStatus::Ready;
    ready.providersStatus = runtime::RulesLoadStatus::Failed;
    state.SetRules(true, ready);
    REQUIRE(observer.last.rules.rules);
    CHECK(observer.last.rules.rules->empty());
    CHECK(observer.last.rules.providersStatus == runtime::RulesLoadStatus::Failed);
    const int calls = observer.calls;
    state.SetRules(true, ready);
    CHECK(observer.calls == calls);
    state.SetRules(false, ready);
    CHECK_FALSE(observer.last.rulesAvailable);
    CHECK(observer.last.rules == runtime::RulesSnapshot{});
}

TEST_CASE("page registry exposes the shell pages in navigation order") {
    const auto& pages = PageRegistry::All();
    REQUIRE(pages.size() == 7);

    const PageId expected[] = {PageId::Overview, PageId::Proxies, PageId::Connections,
                               PageId::Rules, PageId::Profiles, PageId::Logs, PageId::Settings};
    for (std::size_t i = 0; i < pages.size(); ++i) {
        CHECK(pages[i].id == expected[i]);
        CHECK(PageRegistry::IndexOf(expected[i]) == static_cast<int>(i));
        CHECK_FALSE(pages[i].title.empty());
        CHECK_FALSE(pages[i].description.empty());
        CHECK(pages[i].create != nullptr);
    }

    CHECK(PageRegistry::Get(PageId::Settings).title == "Settings");
    CHECK(PageRegistry::IndexOf(PageId::Settings) == 6);
    CHECK(PageRegistry::Get(PageId::Rules).title == "Rules");
    CHECK(std::string(PageIdName(PageId::Rules)) == "rules");
    CHECK(std::string(PageIdName(PageId::Proxies)) == "proxies");
    CHECK(std::string(PageIdName(PageId::Settings)) == "settings");
}
