#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clashyuka/runtime/proxy_service.h"

using namespace clashyuka;
using namespace clashyuka::runtime;

namespace {

template <typename Fn>
bool WaitUntil(Fn predicate, int timeoutMs = 3000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

mihomo::ProxyInfo MakeProxy(const std::string& name, const std::string& type,
                            std::vector<std::string> all = {}, std::string now = {},
                            std::string fixed = {}) {
    mihomo::ProxyInfo proxy;
    proxy.name = name;
    proxy.type = type;
    proxy.all = std::move(all);
    proxy.now = std::move(now);
    proxy.fixed = std::move(fixed);
    proxy.alive = true;
    proxy.udp = true;
    return proxy;
}

struct FakeProxyApi final : ProxyApi {
    mutable std::mutex mutex;
    std::vector<mihomo::ProxyInfo> proxies;
    std::atomic<bool> failGet{false};
    std::atomic<bool> failSelect{false};
    std::atomic<bool> failDelay{false};
    std::atomic<int> delayMs{5};
    std::atomic<int> delayResult{42};
    std::atomic<int> getCalls{0};
    std::atomic<int> selectCalls{0};
    std::atomic<int> clearCalls{0};
    std::atomic<int> delayCalls{0};
    std::string lastGroup;
    std::string lastTarget;
    std::string lastUrl;
    int lastTimeout = 0;

    core::Result<mihomo::ProxyCollection> GetProxies() override {
        ++getCalls;
        if (failGet.load()) {
            return core::MakeError(core::ErrorKind::ConnectionFailed, "fake get failure");
        }
        std::lock_guard<std::mutex> lock(mutex);
        mihomo::ProxyCollection collection;
        collection.proxies = proxies;
        return collection;
    }

    core::Result<void> SelectProxy(std::string_view group, std::string_view target) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++selectCalls;
            lastGroup = std::string(group);
            lastTarget = std::string(target);
        }
        if (failSelect.load()) {
            return core::MakeError(core::ErrorKind::HttpStatus, "fake select failure");
        }
        std::lock_guard<std::mutex> lock(mutex);
        for (mihomo::ProxyInfo& proxy : proxies) {
            if (proxy.name == group) {
                proxy.now = std::string(target);
            }
        }
        return {};
    }

    core::Result<void> ClearSelection(std::string_view group) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++clearCalls;
        lastGroup = std::string(group);
        for (mihomo::ProxyInfo& proxy : proxies) {
            if (proxy.name == group) {
                proxy.fixed.clear();
            }
        }
        return {};
    }

    core::Result<int> TestDelay(std::string_view name, std::string_view url,
                                int timeoutMs) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++delayCalls;
            lastUrl = std::string(url);
            lastTimeout = timeoutMs;
        }
        const int sleep = delayMs.load();
        if (sleep > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
        }
        if (failDelay.load()) {
            return core::MakeError(core::ErrorKind::Timeout, "fake delay timeout");
        }
        (void)name;
        return delayResult.load();
    }
};

struct RecordingObserver final : ProxyServiceObserver {
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<const ProxyCatalog>> catalogs;

    void OnProxyCatalog(const std::shared_ptr<const ProxyCatalog>& catalog) override {
        std::lock_guard<std::mutex> lock(mutex);
        catalogs.push_back(catalog);
    }

    std::shared_ptr<const ProxyCatalog> Last() const {
        std::lock_guard<std::mutex> lock(mutex);
        return catalogs.empty() ? nullptr : catalogs.back();
    }
};

struct Harness {
    std::unique_ptr<FakeProxyApi> apiOwner = std::make_unique<FakeProxyApi>();
    FakeProxyApi* api = apiOwner.get();
    RecordingObserver observer;
    std::unique_ptr<ProxyService> service;

    Harness() {
        api->proxies = {
            MakeProxy("DIRECT", "Direct"),
            MakeProxy("Auto", "URLTest", {"DIRECT", "HK 01"}, "HK 01", "HK 01"),
            MakeProxy("Pick", "Selector", {"DIRECT", "HK 01", "JP 01"}, "DIRECT"),
            MakeProxy("Balance", "LoadBalance", {"DIRECT", "HK 01"}),
            MakeProxy("Future", "Mystery", {"DIRECT"}),
            MakeProxy("HK 01", "Trojan"),
            MakeProxy("JP 01", "Shadowsocks"),
        };
        service = std::make_unique<ProxyService>(
            std::move(apiOwner), [](std::function<void()> task) { task(); },
            "https://www.gstatic.com/generate_204", 5000);
        service->SetObserver(&observer);
    }

    ~Harness() {
        if (service) {
            service->Shutdown();
        }
    }

    bool WaitLoaded() {
        return WaitUntil([this] {
            const auto catalog = observer.Last();
            return catalog != nullptr && catalog->loaded && !catalog->loading;
        });
    }
};

}  // namespace

TEST_CASE("proxy service loads groups and preserves optional fields") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    const auto catalog = harness.observer.Last();
    REQUIRE(catalog != nullptr);
    CHECK(catalog->proxies.size() == 7);
    CHECK(catalog->error.empty());
    CHECK(catalog->structureRevision >= 1);
    CHECK(catalog->updatedAtMs > 0);

    const auto autoGroup = std::find_if(
        catalog->proxies.begin(), catalog->proxies.end(),
        [](const mihomo::ProxyInfo& proxy) { return proxy.name == "Auto"; });
    REQUIRE(autoGroup != catalog->proxies.end());
    CHECK(autoGroup->type == "URLTest");
    CHECK(autoGroup->now == "HK 01");
    CHECK(autoGroup->fixed == "HK 01");
    CHECK(autoGroup->all.size() == 2);
}

TEST_CASE("proxy service keeps the previous catalog on refresh failure") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());
    const auto previous = harness.observer.Last();

    {
        std::lock_guard<std::mutex> lock(harness.api->mutex);
        harness.api->failGet = true;
    }
    harness.service->Refresh();
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return catalog != previous && !catalog->loading && !catalog->error.empty();
    }));
    const auto failed = harness.observer.Last();
    CHECK(failed->proxies.size() == previous->proxies.size());
    CHECK(failed->loaded);
    CHECK_FALSE(failed->error.empty());
}

TEST_CASE("proxy service selects a selector member and reloads from the API") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    harness.service->Select("Pick", "JP 01");
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return std::any_of(catalog->proxies.begin(), catalog->proxies.end(),
                           [](const mihomo::ProxyInfo& proxy) {
                               return proxy.name == "Pick" && proxy.now == "JP 01";
                           });
    }));
    CHECK(harness.api->selectCalls == 1);
    CHECK(harness.api->lastGroup == "Pick");
    CHECK(harness.api->lastTarget == "JP 01");
}

TEST_CASE("proxy service reports selection failures without losing data") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());
    const auto previous = harness.observer.Last();

    {
        std::lock_guard<std::mutex> lock(harness.api->mutex);
        harness.api->failSelect = true;
    }
    harness.service->Select("Pick", "JP 01");
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return catalog != previous && !catalog->error.empty();
    }));
    CHECK(harness.observer.Last()->proxies.size() == previous->proxies.size());
    CHECK(harness.observer.Last()->error.find("select") != std::string::npos);
}

TEST_CASE("proxy service clears a fixed auto selection") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    harness.service->ClearSelection("Auto");
    REQUIRE(WaitUntil([&] { return harness.api->clearCalls == 1; }));
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return std::any_of(catalog->proxies.begin(), catalog->proxies.end(),
                           [](const mihomo::ProxyInfo& proxy) {
                               return proxy.name == "Auto" && proxy.fixed.empty();
                           });
    }));
}

TEST_CASE("proxy service reports delay results and timeouts") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    harness.api->delayResult = 87;
    harness.service->TestDelay("HK 01");
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        const auto it = catalog->delays.find("HK 01");
        return it != catalog->delays.end() && it->second == 87;
    }));
    CHECK(harness.api->lastUrl == "https://www.gstatic.com/generate_204");
    CHECK(harness.api->lastTimeout == 5000);
    CHECK(harness.observer.Last()->testing.empty());

    {
        std::lock_guard<std::mutex> lock(harness.api->mutex);
        harness.api->failDelay = true;
    }
    harness.service->TestDelay("JP 01");
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        const auto it = catalog->delays.find("JP 01");
        return it != catalog->delays.end() && it->second == -1;
    }));
}

TEST_CASE("proxy service filters built-in members and cancels delay tests") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    harness.api->delayMs = 120;
    harness.service->TestGroup({"HK 01", "JP 01", "DIRECT", "REJECT", "HK 01"});
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return catalog->testing.size() == 2;
    }));

    harness.service->CancelTests();
    REQUIRE(WaitUntil([&] { return harness.observer.Last()->testing.empty(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto after = harness.observer.Last();
    CHECK(after->delays.empty());
    CHECK(harness.api->delayCalls.load() <= 2);
}

TEST_CASE("a refresh supersedes earlier delay results") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    harness.api->delayMs = 80;
    harness.service->TestDelay("HK 01");
    REQUIRE(WaitUntil([&] { return harness.observer.Last()->testing.size() == 1; }));

    const std::uint64_t previousGeneration = harness.observer.Last()->generation;
    harness.service->Refresh();
    REQUIRE(WaitUntil([&] {
        const auto catalog = harness.observer.Last();
        return catalog->generation > previousGeneration && catalog->loaded &&
               !catalog->loading;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto catalog = harness.observer.Last();
    CHECK(catalog->delays.find("HK 01") == catalog->delays.end());
    CHECK(catalog->testing.empty());
}

TEST_CASE("proxy service shutdown joins its workers") {
    Harness harness;
    harness.service->Refresh();
    REQUIRE(harness.WaitLoaded());

    const auto start = std::chrono::steady_clock::now();
    harness.service->Shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(2));
    harness.service->Shutdown();  // idempotent
}
