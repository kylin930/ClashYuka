#pragma once

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "clashyuka/core/result.h"
#include "clashyuka/mihomo/api_client.h"
#include "clashyuka/mihomo/types.h"

namespace clashyuka::runtime {

// Immutable catalog shared with the UI. Delay results mutate a copy so the UI
// can read a stable snapshot without holding a lock.
struct ProxyCatalog {
    std::uint64_t generation = 0;
    std::uint64_t structureRevision = 0;
    bool loading = false;
    bool loaded = false;
    std::string error;
    std::int64_t updatedAtMs = 0;
    std::vector<mihomo::ProxyInfo> proxies;
    std::map<std::string, int> delays;
    std::set<std::string> testing;
};

class ProxyServiceObserver {
public:
    virtual ~ProxyServiceObserver() = default;
    virtual void OnProxyCatalog(const std::shared_ptr<const ProxyCatalog>& catalog) = 0;
};

// Thin injectable boundary over MihomoApiClient for tests.
class ProxyApi {
public:
    virtual ~ProxyApi() = default;
    virtual core::Result<mihomo::ProxyCollection> GetProxies() = 0;
    virtual core::Result<void> SelectProxy(std::string_view group, std::string_view target) = 0;
    virtual core::Result<void> ClearSelection(std::string_view group) = 0;
    virtual core::Result<int> TestDelay(std::string_view name, std::string_view url,
                                        int timeoutMs) = 0;
};

std::unique_ptr<ProxyApi> CreateMihomoProxyApi(mihomo::MihomoApiConfig config);

// Loads proxy groups/serializes selection operations on one worker and runs
// delay tests on a small, fixed pool (3 threads). No UI thread does network IO.
class ProxyService final {
public:
    using Dispatcher = std::function<void(std::function<void()>)>;

    ProxyService(std::unique_ptr<ProxyApi> api, Dispatcher dispatcher, std::string testUrl,
                 int testTimeoutMs);
    ~ProxyService();

    ProxyService(const ProxyService&) = delete;
    ProxyService& operator=(const ProxyService&) = delete;

    void SetObserver(ProxyServiceObserver* observer);
    void SetTestSettings(std::string testUrl, int testTimeoutMs);

    void Refresh();
    void Select(std::string group, std::string target);
    void ClearSelection(std::string group);
    void TestDelay(std::string name);
    void TestGroup(std::vector<std::string> names);
    void CancelTests();

    void Shutdown();

    std::shared_ptr<const ProxyCatalog> Snapshot() const;

private:
    struct SharedState;
    enum class LoadCommandType { Refresh, Select, Clear };
    struct LoadCommand {
        LoadCommandType type = LoadCommandType::Refresh;
        std::string group;
        std::string target;
    };
    struct DelayTask {
        std::uint64_t generation = 0;
        std::string name;
    };

    void EnqueueLoad(LoadCommand command);
    void LoadWorkerMain();
    void DelayWorkerMain();
    void HandleLoad(const LoadCommand& command, std::uint64_t generation);
    void ApplyLoadedProxies(std::uint64_t generation, mihomo::ProxyCollection collection);
    void Publish();
    void PublishCopy(const std::shared_ptr<ProxyCatalog>& catalog);
    void SetError(std::uint64_t generation, std::string message);
    std::string EffectiveTestUrl() const;
    int EffectiveTestTimeout() const;

    std::unique_ptr<ProxyApi> api_;
    Dispatcher dispatcher_;
    std::shared_ptr<SharedState> state_;

    mutable std::mutex testMutex_;
    std::string testUrl_;
    int testTimeoutMs_ = 5000;

    std::thread loadWorker_;
    mutable std::mutex loadMutex_;
    std::condition_variable loadCv_;
    std::deque<LoadCommand> loadQueue_;
    bool loadStop_ = false;
    std::uint64_t nextLoadGeneration_ = 0;

    std::vector<std::thread> delayWorkers_;
    mutable std::mutex delayMutex_;
    std::condition_variable delayCv_;
    std::deque<DelayTask> delayQueue_;
    bool delayStop_ = false;
    std::uint64_t delayGeneration_ = 0;

    mutable std::mutex exitMutex_;
    std::condition_variable exitCv_;
    int workersExited_ = 0;
    int workerCount_ = 4;
    bool shutdown_ = false;
};

}  // namespace clashyuka::runtime
