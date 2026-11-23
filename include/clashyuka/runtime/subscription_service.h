#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "clashyuka/platform/http_download.h"
#include "clashyuka/runtime/profile_store.h"

namespace clashyuka::runtime {

class SubscriptionService {
public:
    using JobId = std::uint64_t;
    using Clock = std::chrono::system_clock;
    using Transport = std::function<core::Result<platform::HttpTextResponse>(
        const std::string& url, const std::string& proxyUrl,
        const platform::HttpsTextLimits& limits, std::atomic<bool>& cancel)>;
    enum class Operation { Add, Edit, Refresh };

    struct Completion {
        JobId job;
        Operation operation;
        std::string profileId;  // Newly allocated ID on successful add.
        bool automatic;
        ProfileRefreshOutcome outcome;
        core::Error error;  // Safe to display; never contains a URL or response text.
    };

    // Call every method on the ProfileStore owner thread. The store must outlive
    // this service. Injected transports must obey cancellation and the limits.
    explicit SubscriptionService(ProfileStore& store, Transport transport = {});
    ~SubscriptionService();
    SubscriptionService(const SubscriptionService&) = delete;
    SubscriptionService& operator=(const SubscriptionService&) = delete;

    bool Available() const noexcept;
    bool Busy() const noexcept;
    bool Busy(std::string_view profileId) const;
    core::Result<JobId> Add(std::string_view name, const SubscriptionSettings& settings,
                            const std::string& proxyUrl = {});
    // Interval-only edits are immediate (no job). A URL change is committed
    // together with the new YAML, only after a successful fetch and validation.
    core::Result<std::optional<JobId>> Edit(std::string_view profileId,
                                           const SubscriptionSettings& settings,
                                           const std::string& proxyUrl = {});
    core::Result<JobId> Refresh(std::string_view profileId, const std::string& proxyUrl = {});
    bool Cancel(JobId job);
    bool Cancel(std::string_view profileId);

    std::vector<Completion> Poll(Clock::time_point now = Clock::now());
    // Poll and enqueue due subscriptions. Pass the current installer/download
    // proxy choice (empty means system proxy). No kernel operations are performed.
    std::vector<Completion> Tick(const std::string& proxyUrl = {},
                                 Clock::time_point now = Clock::now());
    // Cancels queued/in-flight jobs, discards unpublished results, and joins.
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clashyuka::runtime
