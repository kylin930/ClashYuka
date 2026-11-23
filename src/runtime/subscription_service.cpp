#include "clashyuka/runtime/subscription_service.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace clashyuka::runtime {
namespace {

using core::ErrorKind;
using core::MakeError;
using Outcome = ProfileRefreshOutcome;

constexpr platform::HttpsTextLimits kLimits{kMaxProfileBytes, std::chrono::seconds(30)};
constexpr std::size_t kMaxOutstandingJobs = 16;

bool ValidSettings(const SubscriptionSettings& settings) {
    const std::string_view url(settings.url);
    constexpr std::string_view prefix = "https://";
    if (url.size() <= prefix.size() || url.size() > 8192 ||
        settings.refreshInterval.count() < 0 ||
        !std::ranges::equal(url.substr(0, prefix.size()), prefix, {}, [](char ch) {
            return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 'a' - 'A') : ch;
        }) || std::ranges::any_of(url, [](unsigned char ch) {
            return ch <= 32 || ch == 127 || ch == '\\';
        })) {
        return false;
    }
    const auto rest = url.substr(prefix.size());
    const auto authority = rest.substr(0, rest.find_first_of("/?#"));
    return !authority.empty() && authority.find('@') == authority.npos;
}

bool SameSettings(const SubscriptionSettings& lhs, const SubscriptionSettings& rhs) {
    return lhs.url == rhs.url && lhs.refreshInterval == rhs.refreshInterval;
}

core::Error Cancelled() {
    return MakeError(ErrorKind::Cancelled, "subscription download cancelled");
}

core::Error WriteError() {
    return MakeError(ErrorKind::ConfigFailed, "could not save the subscription update");
}

core::Error HttpError(int status) {
    const bool valid = status >= 100 && status <= 599;
    return MakeError(ErrorKind::HttpStatus, valid ? "subscription server returned HTTP " +
                     std::to_string(status) : "subscription server returned an invalid HTTP status",
                     valid ? status : 0);
}

long long Timestamp(SubscriptionService::Clock::time_point now) {
    return std::max(1LL, static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count()));
}

}  // namespace

struct SubscriptionService::Impl {
    struct Job {
        JobId number = 0;
        Operation operation;
        std::string profileId;
        std::string name;
        SubscriptionSettings settings;
        std::optional<SubscriptionSettings> original;
        std::string proxy;
        bool automatic = false;
        std::atomic<bool> cancelled{false};
    };

    struct Finished {
        std::shared_ptr<Job> job;
        Outcome outcome;
        core::Error error;
        std::string bytes;
    };

    Impl(ProfileStore& store_, Transport transport_)
        : store(store_), transport(transport_ ? std::move(transport_) :
                    (platform::HttpsTextAvailable() ? Transport(platform::HttpGetBoundedHttpsText)
                                                    : Transport{})) {}

    core::Result<JobId> Enqueue(Operation operation, std::string_view id, std::string_view name,
                                const SubscriptionSettings& settings,
                                std::optional<SubscriptionSettings> original,
                                const std::string& proxy, bool automatic = false) {
        if (stopping || !transport) {
            return MakeError(ErrorKind::DownloadFailed, "subscription downloads are unavailable");
        }
        if (!ValidSettings(settings)) {
            return MakeError(ErrorKind::InvalidConfig, "subscription requires a valid HTTPS URL and refresh interval");
        }
        if ((!id.empty() && Find(id)) || active.size() >= kMaxOutstandingJobs) {
            return MakeError(ErrorKind::DownloadFailed, "a subscription job is already pending or the queue is full");
        }
        auto job = std::make_shared<Job>();
        job->number = next++;
        job->operation = operation;
        job->profileId = id;
        job->name = name;
        job->settings = settings;
        job->original = std::move(original);
        job->proxy = proxy;
        job->automatic = automatic;
        if (!worker.joinable()) {
            try {
                worker = std::thread([this] { Work(); });
            } catch (const std::system_error&) {
                return MakeError(ErrorKind::Internal, "could not start the subscription worker");
            }
        }
        active.emplace(job->number, job);
        {
            std::lock_guard lock(mutex);
            waiting.push_back(job);
        }
        wake.notify_one();
        return job->number;
    }

    std::shared_ptr<Job> Find(std::string_view id) const {
        if (!id.empty()) {
            for (const auto& [number, job] : active) {
                if (job->profileId == id) {
                    return job;
                }
            }
        }
        return {};
    }

    Finished Fetch(const std::shared_ptr<Job>& job) const {
        try {
            if (job->cancelled.load()) {
                return {job, Outcome::Cancelled, Cancelled(), {}};
            }
            auto result = transport(job->settings.url, job->proxy, kLimits, job->cancelled);
            if (job->cancelled.load() || (!result && result.error().kind == ErrorKind::Cancelled)) {
                return {job, Outcome::Cancelled, Cancelled(), {}};
            }
            if (!result) {
                switch (result.error().kind) {
                    case ErrorKind::HttpStatus:
                        return {job, Outcome::HttpFailed, HttpError(result.error().httpStatus), {}};
                    case ErrorKind::InvalidConfig:
                        return {job, Outcome::InvalidContent, MakeError(ErrorKind::InvalidConfig,
                                "subscription URL is invalid or its response exceeds the size limit"), {}};
                    case ErrorKind::Timeout:
                        return {job, Outcome::NetworkFailed, MakeError(ErrorKind::Timeout,
                                "subscription download timed out"), {}};
                    default:
                        break;
                }
            } else {
                const auto status = result.value().status;
                if (status < 200 || status >= 300) {
                    return {job, Outcome::HttpFailed, HttpError(status), {}};
                }
                auto bytes = std::move(result.value().body);
                if (!ValidateProfileYaml(bytes)) {
                    return {job, Outcome::InvalidContent, MakeError(ErrorKind::InvalidConfig,
                            "subscription response is not a supported Mihomo YAML profile"), {}};
                }
                return {job, Outcome::Succeeded, {}, std::move(bytes)};
            }
        } catch (...) {
            // Neither transport exceptions nor parser diagnostics are safe to display.
        }
        return {job, Outcome::NetworkFailed, MakeError(ErrorKind::DownloadFailed,
                "could not download the subscription"), {}};
    }

    void Work() {
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [this] { return stopping || !waiting.empty(); });
            if (stopping) {
                return;
            }
            auto job = std::move(waiting.front());
            waiting.pop_front();
            lock.unlock();
            auto result = Fetch(job);
            lock.lock();
            if (!stopping) {
                completed.push_back(std::move(result));
            }
        }
    }

    Completion Publish(Finished& result, long long timestamp) {
        const auto& job = *result.job;
        Completion published{job.number, job.operation, job.profileId, job.automatic,
                             result.outcome, std::move(result.error)};
        const auto cancel = [&] {
            published.outcome = Outcome::Cancelled;
            published.error = Cancelled();
        };
        const auto writeFailed = [&] {
            published.outcome = Outcome::WriteFailed;
            published.error = WriteError();
        };
        if (job.cancelled.load()) {
            cancel();
        }
        if (job.operation == Operation::Add) {
            if (published.outcome == Outcome::Succeeded) {
                auto added = store.AddSubscription(job.name, job.settings, result.bytes,
                                                   {published.outcome, timestamp});
                if (added) {
                    published.profileId = added.value().id;
                } else {
                    writeFailed();
                }
            }
            return published;
        }
        const auto current = store.GetSubscriptionSettings(job.profileId);
        if (!current || !SameSettings(current.value(), *job.original)) {
            cancel();
            return published;
        }
        if (published.outcome == Outcome::Succeeded) {
            auto updated = store.ReplaceSubscriptionContent(job.profileId, result.bytes,
                {published.outcome, timestamp}, job.operation == Operation::Edit ?
                    std::optional(job.settings) : std::nullopt);
            if (updated) {
                lastAttempt.erase(job.profileId);
                return published;
            }
            writeFailed();
        }
        if (store.RecordSubscriptionStatus(job.profileId, {published.outcome, timestamp})) {
            lastAttempt.erase(job.profileId);
        } else {
            // Keep the underlying failure classification, but avoid tight retries.
            lastAttempt[job.profileId] = timestamp;
        }
        return published;
    }

    ProfileStore& store;
    Transport transport;
    JobId next = 1;
    std::unordered_map<JobId, std::shared_ptr<Job>> active;
    std::unordered_map<std::string, long long> lastAttempt;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::deque<std::shared_ptr<Job>> waiting;
    std::deque<Finished> completed;
    std::thread worker;
};

SubscriptionService::SubscriptionService(ProfileStore& store, Transport transport)
    : impl_(std::make_unique<Impl>(store, std::move(transport))) {}

SubscriptionService::~SubscriptionService() { Shutdown(); }

bool SubscriptionService::Available() const noexcept { return impl_->transport && !impl_->stopping; }
bool SubscriptionService::Busy() const noexcept { return !impl_->active.empty(); }
bool SubscriptionService::Busy(std::string_view id) const { return !!impl_->Find(id); }

core::Result<SubscriptionService::JobId> SubscriptionService::Add(
    std::string_view name, const SubscriptionSettings& settings, const std::string& proxyUrl) {
    if (name.find_first_not_of(' ') == name.npos) {
        return MakeError(ErrorKind::InvalidConfig, "subscription name is required");
    }
    return impl_->Enqueue(Operation::Add, {}, name, settings, std::nullopt, proxyUrl);
}

core::Result<std::optional<SubscriptionService::JobId>> SubscriptionService::Edit(
    std::string_view id, const SubscriptionSettings& settings, const std::string& proxyUrl) {
    if (!Available()) {
        return MakeError(ErrorKind::DownloadFailed, "subscription downloads are unavailable");
    }
    if (Busy(id)) {
        return MakeError(ErrorKind::DownloadFailed, "a subscription job is already pending");
    }
    if (!ValidSettings(settings)) {
        return MakeError(ErrorKind::InvalidConfig, "subscription requires a valid HTTPS URL and refresh interval");
    }
    auto original = impl_->store.GetSubscriptionSettings(id);
    if (!original) {
        return original.error();
    }
    if (original.value().url == settings.url) {
        if (!SameSettings(original.value(), settings)) {
            auto updated = impl_->store.UpdateSubscriptionSettings(id, settings);
            if (!updated) {
                return WriteError();
            }
        }
        return std::optional<JobId>{};
    }
    auto queued = impl_->Enqueue(Operation::Edit, id, {}, settings, original.Take(), proxyUrl);
    if (!queued) {
        return queued.error();
    }
    return std::optional<JobId>{queued.value()};
}

core::Result<SubscriptionService::JobId> SubscriptionService::Refresh(
    std::string_view id, const std::string& proxyUrl) {
    auto settings = impl_->store.GetSubscriptionSettings(id);
    if (!settings) {
        return settings.error();
    }
    return impl_->Enqueue(Operation::Refresh, id, {}, settings.value(), settings.value(), proxyUrl);
}

bool SubscriptionService::Cancel(JobId job) {
    const auto it = impl_->active.find(job);
    return it != impl_->active.end() && !it->second->cancelled.exchange(true);
}

bool SubscriptionService::Cancel(std::string_view id) {
    const auto job = impl_->Find(id);
    return job && !job->cancelled.exchange(true);
}

std::vector<SubscriptionService::Completion> SubscriptionService::Poll(Clock::time_point now) {
    std::deque<Impl::Finished> completed;
    {
        std::lock_guard lock(impl_->mutex);
        completed.swap(impl_->completed);
    }
    std::vector<Completion> results;
    results.reserve(completed.size());
    for (auto& item : completed) {
        results.push_back(impl_->Publish(item, Timestamp(now)));
        impl_->active.erase(item.job->number);
    }
    return results;
}

std::vector<SubscriptionService::Completion> SubscriptionService::Tick(
    const std::string& proxyUrl, Clock::time_point now) {
    auto results = Poll(now);
    if (!Available()) {
        return results;
    }
    const auto timestamp = Timestamp(now);
    const auto profiles = impl_->store.ListProfiles();
    std::erase_if(impl_->lastAttempt, [&profiles](const auto& row) {
        return std::ranges::none_of(profiles, [&row](const auto& profile) { return profile.id == row.first; });
    });
    for (const auto& profile : profiles) {
        if (profile.kind != ProfileKind::Subscription || Busy(profile.id)) {
            continue;
        }
        const auto settings = impl_->store.GetSubscriptionSettings(profile.id);
        if (!settings || settings.value().refreshInterval.count() == 0) {
            continue;
        }
        auto last = profile.refresh.completedAt;
        if (last == 0 || impl_->lastAttempt.contains(profile.id)) {
            const auto [it, inserted] = impl_->lastAttempt.try_emplace(profile.id, timestamp);
            auto& baseline = it->second;
            baseline = std::min(baseline, timestamp);
            last = std::max(last, baseline);
        }
        if (timestamp >= last && timestamp - last >= settings.value().refreshInterval.count()) {
            impl_->Enqueue(Operation::Refresh, profile.id, {}, settings.value(), settings.value(),
                           proxyUrl, true);
        }
    }
    return results;
}

void SubscriptionService::Shutdown() {
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping) {
            return;
        }
        impl_->stopping = true;
        for (const auto& [number, job] : impl_->active) {
            job->cancelled.store(true);
        }
        impl_->waiting.clear();
        impl_->completed.clear();
    }
    impl_->wake.notify_one();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->active.clear();
}

}  // namespace clashyuka::runtime
