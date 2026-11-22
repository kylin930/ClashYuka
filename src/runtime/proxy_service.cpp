#include "clashyuka/runtime/proxy_service.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <utility>

namespace clashyuka::runtime {
namespace {

using Clock = std::chrono::system_clock;

std::int64_t NowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
}

bool IsBuiltInName(const std::string& name) {
    return name == "DIRECT" || name == "REJECT" || name == "REJECT-DROP" ||
           name == "PASS" || name == "COMPATIBLE";
}

class MihomoProxyApi final : public ProxyApi {
public:
    explicit MihomoProxyApi(mihomo::MihomoApiConfig config) : client_(std::move(config)) {}

    core::Result<mihomo::ProxyCollection> GetProxies() override { return client_.GetProxies(); }

    core::Result<void> SelectProxy(std::string_view group, std::string_view target) override {
        return client_.SelectProxy(group, target);
    }

    core::Result<void> ClearSelection(std::string_view group) override {
        return client_.ClearProxySelection(group);
    }

    core::Result<int> TestDelay(std::string_view name, std::string_view url,
                                int timeoutMs) override {
        return client_.TestProxyDelay(name, url, timeoutMs);
    }

private:
    mihomo::MihomoApiClient client_;
};

}  // namespace

struct ProxyService::SharedState {
    mutable std::mutex mutex;
    std::shared_ptr<const ProxyCatalog> catalog = std::make_shared<ProxyCatalog>();
    ProxyServiceObserver* observer = nullptr;
};

std::unique_ptr<ProxyApi> CreateMihomoProxyApi(mihomo::MihomoApiConfig config) {
    return std::make_unique<MihomoProxyApi>(std::move(config));
}

ProxyService::ProxyService(std::unique_ptr<ProxyApi> api, Dispatcher dispatcher,
                           std::string testUrl, int testTimeoutMs)
    : api_(std::move(api)),
      dispatcher_(std::move(dispatcher)),
      state_(std::make_shared<SharedState>()),
      testUrl_(std::move(testUrl)),
      testTimeoutMs_(testTimeoutMs > 0 ? testTimeoutMs : 5000) {
    for (int i = 0; i < 3; ++i) {
        delayWorkers_.emplace_back([this] { DelayWorkerMain(); });
    }
    loadWorker_ = std::thread([this] { LoadWorkerMain(); });
    workerCount_ = static_cast<int>(delayWorkers_.size()) + 1;
}

ProxyService::~ProxyService() {
    Shutdown();
}

void ProxyService::SetObserver(ProxyServiceObserver* observer) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->observer = observer;
    }
    if (observer != nullptr) {
        Publish();
    }
}

void ProxyService::SetTestSettings(std::string testUrl, int testTimeoutMs) {
    std::lock_guard<std::mutex> lock(testMutex_);
    testUrl_ = std::move(testUrl);
    testTimeoutMs_ = testTimeoutMs > 0 ? testTimeoutMs : 5000;
}

void ProxyService::Refresh() {
    EnqueueLoad(LoadCommand{});
}

void ProxyService::Select(std::string group, std::string target) {
    LoadCommand command;
    command.type = LoadCommandType::Select;
    command.group = std::move(group);
    command.target = std::move(target);
    EnqueueLoad(std::move(command));
}

void ProxyService::ClearSelection(std::string group) {
    LoadCommand command;
    command.type = LoadCommandType::Clear;
    command.group = std::move(group);
    EnqueueLoad(std::move(command));
}

void ProxyService::TestDelay(std::string name) {
    TestGroup({std::move(name)});
}

void ProxyService::TestGroup(std::vector<std::string> names) {
    if (shutdown_) {
        return;
    }
    std::uint64_t generation = 0;
    std::uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(delayMutex_);
        epoch = delayGeneration_;
    }
    std::shared_ptr<ProxyCatalog> updated;
    std::vector<std::string> queued;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->catalog->proxies.empty()) {
            return;
        }
        generation = state_->catalog->generation;
        updated = std::make_shared<ProxyCatalog>(*state_->catalog);
        for (const std::string& name : names) {
            if (name.empty() || IsBuiltInName(name) || updated->testing.count(name) != 0) {
                continue;
            }
            updated->testing.insert(name);
            queued.push_back(name);
        }
        if (queued.empty()) {
            return;
        }
        state_->catalog = updated;
    }
    PublishCopy(updated);

    {
        std::lock_guard<std::mutex> lock(delayMutex_);
        if (delayGeneration_ != epoch) {
            return;
        }
        for (const std::string& name : queued) {
            delayQueue_.push_back(DelayTask{generation, name});
        }
    }
    delayCv_.notify_all();
}

void ProxyService::CancelTests() {
    {
        std::lock_guard<std::mutex> lock(delayMutex_);
        ++delayGeneration_;
        delayQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->catalog->testing.empty()) {
            return;
        }
        auto updated = std::make_shared<ProxyCatalog>(*state_->catalog);
        updated->testing.clear();
        state_->catalog = updated;
    }
    Publish();
}

void ProxyService::Shutdown() {
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        loadStop_ = true;
        loadQueue_.clear();
    }
    loadCv_.notify_all();
    {
        std::lock_guard<std::mutex> lock(delayMutex_);
        delayStop_ = true;
        ++delayGeneration_;
        delayQueue_.clear();
    }
    delayCv_.notify_all();

    {
        std::unique_lock<std::mutex> lock(exitMutex_);
        exitCv_.wait_for(lock, std::chrono::seconds(15),
                         [this] { return workersExited_ >= workerCount_; });
    }
    if (loadWorker_.joinable()) {
        loadWorker_.join();
    }
    for (std::thread& worker : delayWorkers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->observer = nullptr;
    }
}

std::shared_ptr<const ProxyCatalog> ProxyService::Snapshot() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->catalog;
}

void ProxyService::EnqueueLoad(LoadCommand command) {
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        if (loadStop_) {
            return;
        }
        loadQueue_.push_back(std::move(command));
    }
    loadCv_.notify_all();
}

void ProxyService::LoadWorkerMain() {
    for (;;) {
        LoadCommand command;
        {
            std::unique_lock<std::mutex> lock(loadMutex_);
            loadCv_.wait(lock, [this] { return loadStop_ || !loadQueue_.empty(); });
            if (loadStop_) {
                break;
            }
            command = std::move(loadQueue_.front());
            loadQueue_.pop_front();
        }

        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            generation = ++nextLoadGeneration_;
            auto loading = std::make_shared<ProxyCatalog>(*state_->catalog);
            loading->generation = generation;
            loading->loading = true;
            loading->error.clear();
            state_->catalog = loading;
        }
        Publish();

        try {
            HandleLoad(command, generation);
        } catch (const std::exception& error) {
            SetError(generation, std::string("proxy worker error: ") + error.what());
        } catch (...) {
            SetError(generation, "proxy worker error");
        }
    }
    {
        std::lock_guard<std::mutex> lock(exitMutex_);
        ++workersExited_;
    }
    exitCv_.notify_all();
}

void ProxyService::HandleLoad(const LoadCommand& command, std::uint64_t generation) {
    if (command.type == LoadCommandType::Select) {
        const core::Result<void> result = api_->SelectProxy(command.group, command.target);
        if (!result) {
            SetError(generation, result.error().message);
            return;
        }
    } else if (command.type == LoadCommandType::Clear) {
        const core::Result<void> result = api_->ClearSelection(command.group);
        if (!result) {
            SetError(generation, result.error().message);
            return;
        }
    }

    core::Result<mihomo::ProxyCollection> proxies = api_->GetProxies();
    if (!proxies) {
        SetError(generation, proxies.error().message);
        return;
    }
    ApplyLoadedProxies(generation, proxies.Take());
}

void ProxyService::ApplyLoadedProxies(std::uint64_t generation,
                                      mihomo::ProxyCollection collection) {
    auto updated = std::make_shared<ProxyCatalog>();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->catalog->generation != generation) {
            return;
        }
        updated->generation = generation;
        updated->structureRevision = state_->catalog->structureRevision + 1;
        updated->proxies = std::move(collection.proxies);
        updated->loaded = true;
        updated->loading = false;
        updated->updatedAtMs = NowMillis();
        state_->catalog = updated;
    }
    PublishCopy(updated);
}

void ProxyService::SetError(std::uint64_t generation, std::string message) {
    auto updated = std::make_shared<ProxyCatalog>();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->catalog->generation != generation) {
            return;
        }
        updated = std::make_shared<ProxyCatalog>(*state_->catalog);
        updated->loading = false;
        updated->error = std::move(message);
        state_->catalog = updated;
    }
    PublishCopy(updated);
}

void ProxyService::DelayWorkerMain() {
    for (;;) {
        DelayTask task;
        std::uint64_t epoch = 0;
        {
            std::unique_lock<std::mutex> lock(delayMutex_);
            delayCv_.wait(lock, [this] { return delayStop_ || !delayQueue_.empty(); });
            if (delayStop_) {
                break;
            }
            task = std::move(delayQueue_.front());
            delayQueue_.pop_front();
            epoch = delayGeneration_;
        }

        const std::string url = EffectiveTestUrl();
        const int timeout = EffectiveTestTimeout();
        core::Result<int> result = api_->TestDelay(task.name, url, timeout);

        bool accepted = false;
        std::shared_ptr<ProxyCatalog> updated;
        {
            std::lock_guard<std::mutex> lock(delayMutex_);
            if (delayGeneration_ != epoch) {
                continue;
            }
        }
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->catalog->generation != task.generation) {
                continue;
            }
            updated = std::make_shared<ProxyCatalog>(*state_->catalog);
            updated->testing.erase(task.name);
            updated->delays[task.name] = result ? result.value() : -1;
            state_->catalog = updated;
            accepted = true;
        }
        if (accepted) {
            PublishCopy(updated);
        }
    }
    {
        std::lock_guard<std::mutex> lock(exitMutex_);
        ++workersExited_;
    }
    exitCv_.notify_all();
}

void ProxyService::Publish() {
    if (!dispatcher_) {
        return;
    }
    std::shared_ptr<const ProxyCatalog> catalog;
    ProxyServiceObserver* observer = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        catalog = state_->catalog;
        observer = state_->observer;
    }
    if (observer == nullptr) {
        return;
    }
    auto state = state_;
    try {
        dispatcher_([state, catalog] {
            ProxyServiceObserver* current = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->catalog != catalog || state->observer == nullptr) {
                    return;
                }
                current = state->observer;
            }
            current->OnProxyCatalog(catalog);
        });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[clashyuka] proxy UI dispatch failed: %s\n", error.what());
    }
}

void ProxyService::PublishCopy(const std::shared_ptr<ProxyCatalog>& catalog) {
    if (!dispatcher_) {
        return;
    }
    ProxyServiceObserver* observer = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        observer = state_->observer;
    }
    if (observer == nullptr) {
        return;
    }
    std::shared_ptr<const ProxyCatalog> shared = catalog;
    auto state = state_;
    try {
        dispatcher_([state, shared] {
            ProxyServiceObserver* current = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->catalog != shared || state->observer == nullptr) {
                    return;
                }
                current = state->observer;
            }
            current->OnProxyCatalog(shared);
        });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[clashyuka] proxy UI dispatch failed: %s\n", error.what());
    }
}

std::string ProxyService::EffectiveTestUrl() const {
    std::lock_guard<std::mutex> lock(testMutex_);
    return testUrl_.empty() ? "https://www.gstatic.com/generate_204" : testUrl_;
}

int ProxyService::EffectiveTestTimeout() const {
    std::lock_guard<std::mutex> lock(testMutex_);
    return testTimeoutMs_ > 0 ? testTimeoutMs_ : 5000;
}

}  // namespace clashyuka::runtime
