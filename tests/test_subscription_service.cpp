#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <random>
#include <thread>

#include "clashyuka/runtime/subscription_service.h"

using namespace clashyuka;
using namespace runtime;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
constexpr auto kUrl = "https://example.test/sub?token=secret-token";
constexpr auto kOtherUrl = "https://example.test/other?token=secret-token";
constexpr auto kOriginal = "rules: [MATCH,DIRECT]\n";
constexpr auto kFresh = "rules: []\n";
constexpr auto kNow = SubscriptionService::Clock::time_point(100s);
using Outcome = ProfileRefreshOutcome;

struct Fixture {
    fs::path directory = fs::temp_directory_path() /
        ("clashyuka-subscription-" + std::to_string(std::random_device{}()));
    std::unique_ptr<ProfileStore> store;
    Fixture() {
        REQUIRE(fs::create_directory(directory));
        auto opened = ProfileStore::Open(directory);
        REQUIRE(opened);
        store = opened.Take();
    }
    ~Fixture() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
    std::string Seed(std::chrono::seconds interval = 0s) {
        auto result = store->AddSubscription("Test", {kUrl, interval}, kOriginal);
        REQUIRE(result);
        return result.value().id;
    }
};

template<class Predicate>
bool WaitFor(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

SubscriptionService::Completion Wait(SubscriptionService& service,
                                     SubscriptionService::Clock::time_point now = kNow) {
    std::vector<SubscriptionService::Completion> result;
    REQUIRE(WaitFor([&] { result = service.Poll(now); return !result.empty(); }));
    REQUIRE(result.size() == 1);
    return std::move(result.front());
}
}  // namespace

TEST_CASE("subscription work is deduplicated until owner publication and uses the chosen proxy") {
    Fixture fixture;
    std::atomic<bool> received{false};
    const auto owner = std::this_thread::get_id();
    std::atomic<bool> correctRequest{false};
    SubscriptionService service(*fixture.store, [&](const auto& url, const auto& proxy,
                                                    const auto& limits, auto&) {
        correctRequest = url == kUrl && proxy == "http://proxy.test:8080" &&
            limits.maxBytes == kMaxProfileBytes && limits.timeout == 30s &&
            std::this_thread::get_id() != owner;
        received = true;
        return platform::HttpTextResponse{200, kFresh};
    });
    CHECK_FALSE(service.Add("Unsafe", {"http://example.test/?token=secret-token"}));
    REQUIRE(service.Add("Remote", {kUrl}, "http://proxy.test:8080"));
    REQUIRE(WaitFor([&] { return received.load(); }));
    CHECK(correctRequest.load());
    CHECK(service.Busy());
    CHECK(fixture.store->ListProfiles().size() == 1);
    const auto added = Wait(service);
    REQUIRE(added.outcome == Outcome::Succeeded);
    REQUIRE_FALSE(added.profileId.empty());
    CHECK(fixture.store->LoadProfileBytes(added.profileId).value() == kFresh);
    CHECK(fixture.store->SelectedProfileId() == kBuiltInProfileId);
    REQUIRE(service.Refresh(added.profileId, "http://proxy.test:8080"));
    CHECK(service.Busy(added.profileId));
    CHECK_FALSE(service.Refresh(added.profileId));
    CHECK_FALSE(service.Edit(added.profileId, {kOtherUrl}));
    CHECK(Wait(service).outcome == Outcome::Succeeded);
    CHECK_FALSE(service.Busy());
}

TEST_CASE("subscription failures redact transport details and preserve valid content and settings") {
    Fixture fixture;
    const auto id = fixture.Seed();
    int attempt = 0;
    SubscriptionService service(*fixture.store, [&](const auto&, const auto&, const auto&, auto&)
                                     -> core::Result<platform::HttpTextResponse> {
        switch (++attempt) {
            case 1: return core::MakeError(core::ErrorKind::DownloadFailed, kUrl);
            case 2: return platform::HttpTextResponse{401, kUrl};
            case 3: return platform::HttpTextResponse{200, std::string("[bad # ") + kUrl};
            case 4: return platform::HttpTextResponse{200, std::string(kMaxProfileBytes + 1, 'x')};
            default: return platform::HttpTextResponse{200, kFresh};
        }
    });
    for (auto outcome : {Outcome::NetworkFailed, Outcome::HttpFailed,
                         Outcome::InvalidContent, Outcome::InvalidContent}) {
        const auto edit = service.Edit(id, {kOtherUrl, 10s});
        REQUIRE(edit);
        REQUIRE(edit.value().has_value());
        const auto result = Wait(service);
        CHECK(result.outcome == outcome);
        CHECK(result.error.message.find("secret-token") == std::string::npos);
        CHECK(fixture.store->ListProfiles().back().refresh.outcome == outcome);
        CHECK(fixture.store->LoadProfileBytes(id).value() == kOriginal);
        CHECK(fixture.store->GetSubscriptionSettings(id).value().url == kUrl);
    }
    REQUIRE(service.Edit(id, {kOtherUrl, 10s}));
    CHECK(Wait(service).outcome == Outcome::Succeeded);
    CHECK(fixture.store->LoadProfileBytes(id).value() == kFresh);
    CHECK(fixture.store->GetSubscriptionSettings(id).value().url == kOtherUrl);
}

TEST_CASE("subscription write failure rolls back YAML and URL changes") {
    Fixture fixture;
    const auto id = fixture.Seed();
    SubscriptionService service(*fixture.store, [](const auto&, const auto&, const auto&, auto&) {
        return platform::HttpTextResponse{200, kFresh};
    });
    const auto catalog = fixture.directory / "profiles" / "catalog.json";
    REQUIRE(fs::remove(catalog));
    REQUIRE(fs::create_directory(catalog));
    REQUIRE(service.Edit(id, {kOtherUrl}));
    CHECK(Wait(service).outcome == Outcome::WriteFailed);
    CHECK(fixture.store->LoadProfileBytes(id).value() == kOriginal);
    CHECK(fixture.store->GetSubscriptionSettings(id).value().url == kUrl);
}

TEST_CASE("optional timer schedules one refresh when due and records the attempt") {
    Fixture fixture;
    const auto disabled = fixture.Seed();
    const auto timed = fixture.Seed(10s);
    SubscriptionService service(*fixture.store, [](const auto&, const auto&, const auto&, auto&) {
        return platform::HttpTextResponse{500, {}};
    });
    CHECK(service.Tick({}, kNow).empty());
    CHECK(service.Tick({}, kNow + 9s).empty());
    CHECK_FALSE(service.Busy());
    CHECK(service.Tick({}, kNow + 10s).empty());
    CHECK(service.Busy(timed));
    CHECK_FALSE(service.Busy(disabled));
    auto result = Wait(service, kNow + 10s);
    CHECK(result.automatic);
    CHECK(result.outcome == Outcome::HttpFailed);
    CHECK(service.Tick({}, kNow + 19s).empty());
    CHECK_FALSE(service.Busy());
    const auto edited = service.Edit(timed, {kUrl, 0s});
    REQUIRE(edited);
    CHECK_FALSE(edited.value().has_value());
    CHECK(service.Tick({}, kNow + 60s).empty());
    CHECK_FALSE(service.Busy());
}

TEST_CASE("a fetched result can be cancelled before the owner polls") {
    Fixture fixture;
    const auto first = fixture.Seed();
    const auto second = fixture.Seed();
    std::atomic<int> calls{0};
    SubscriptionService service(*fixture.store, [&](const auto&, const auto&, const auto&, auto&) {
        ++calls;
        return platform::HttpTextResponse{200, kFresh};
    });
    const auto job = service.Refresh(first);
    REQUIRE(job);
    REQUIRE(service.Refresh(second));
    // The single worker queues the first completion before starting the second fetch.
    REQUIRE(WaitFor([&] { return calls == 2; }));
    CHECK(service.Cancel(job.value()));
    const auto results = service.Poll(kNow);
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().job == job.value());
    CHECK(results.front().outcome == Outcome::Cancelled);
    CHECK(fixture.store->LoadProfileBytes(first).value() == kOriginal);
}

TEST_CASE("cancel blocks publication and shutdown joins without publishing late work") {
    Fixture fixture;
    const auto id = fixture.Seed();
    std::atomic<int> entered{0};
    std::atomic<int> exited{0};
    SubscriptionService service(*fixture.store, [&](const auto&, const auto&, const auto&, auto& cancel) {
        ++entered;
        while (!cancel.load()) {
            std::this_thread::sleep_for(1ms);
        }
        ++exited;
        return platform::HttpTextResponse{200, kFresh};
    });
    REQUIRE(service.Refresh(id));
    REQUIRE(WaitFor([&] { return entered == 1; }));
    CHECK(service.Cancel(id));
    CHECK(Wait(service).outcome == Outcome::Cancelled);
    CHECK(fixture.store->LoadProfileBytes(id).value() == kOriginal);
    REQUIRE(service.Refresh(id));
    REQUIRE(WaitFor([&] { return entered == 2; }));
    REQUIRE(service.Add("Queued", {kOtherUrl}));
    service.Shutdown();
    CHECK(exited == 2);
    CHECK(entered == 2);
    CHECK_FALSE(service.Busy());
    CHECK_FALSE(service.Available());
    CHECK(service.Poll().empty());
    CHECK(fixture.store->ListProfiles().size() == 2);
    CHECK(fixture.store->LoadProfileBytes(id).value() == kOriginal);
}
