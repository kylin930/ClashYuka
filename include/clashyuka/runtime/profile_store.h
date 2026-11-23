#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "clashyuka/core/result.h"

namespace clashyuka::runtime {

inline constexpr std::string_view kBuiltInProfileId = "builtin";
inline constexpr std::size_t kMaxProfileBytes = 4 * 1024 * 1024;

enum class ProfileKind { BuiltIn, Local, Subscription };
enum class ProfileRefreshOutcome {
    Never, Succeeded, NetworkFailed, HttpFailed, InvalidContent, WriteFailed, Cancelled,
};

struct ProfileRefreshStatus {
    ProfileRefreshOutcome outcome = ProfileRefreshOutcome::Never;
    // Unix time in seconds, zero if no attempt has completed.
    long long completedAt = 0;
};

struct SubscriptionSettings {
    std::string url;
    // Zero disables automatic refresh (the default).
    std::chrono::seconds refreshInterval{0};
};

struct ProfileInfo {
    std::string id;
    std::string name;
    ProfileKind kind = ProfileKind::Local;
    ProfileRefreshStatus refresh;
};

// Store-level checks only; this does not run Mihomo or sanitize runtime settings.
core::Result<void> ValidateProfileYaml(std::string_view bytes);

// One owning instance per user-data directory. Serialize calls on the owner
// thread; workers may fetch/validate bytes but must publish through the owner.
class ProfileStore {
public:
    static core::Result<std::unique_ptr<ProfileStore>> Open(
        const std::filesystem::path& userDataDirectory);
    ~ProfileStore() = default;
    ProfileStore(const ProfileStore&) = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;

    std::vector<ProfileInfo> ListProfiles() const;
    std::string SelectedProfileId() const;
    core::Result<void> SelectProfile(std::string_view id);
    core::Result<std::string> LoadProfileBytes(std::string_view id) const;

    // Imports copy exact bytes without changing selection or the source file.
    // An empty display name uses the filename stem. New names get a suffix on collision.
    core::Result<ProfileInfo> ImportLocal(const std::filesystem::path& source,
                                         std::string_view name = {});
    core::Result<ProfileInfo> AddSubscription(std::string_view name,
                                              const SubscriptionSettings& settings,
                                              std::string_view bytes,
                                              ProfileRefreshStatus status = {});
    core::Result<void> RenameProfile(std::string_view id, std::string_view name);
    // The caller confirms deletion and supplies the running profile, if any.
    core::Result<void> DeleteProfile(std::string_view id, std::string_view runningProfileId);

    // The URL is deliberately not exposed by ListProfiles or errors.
    core::Result<SubscriptionSettings> GetSubscriptionSettings(std::string_view id) const;
    core::Result<void> UpdateSubscriptionSettings(std::string_view id,
                                                  const SubscriptionSettings& settings);
    core::Result<void> RecordSubscriptionStatus(std::string_view id,
                                                ProfileRefreshStatus status);
    // Publishes exact validated bytes and success status as one transaction, with
    // optional settings changes. Any failure leaves the previous entry unchanged.
    core::Result<void> ReplaceSubscriptionContent(
        std::string_view id, std::string_view bytes, ProfileRefreshStatus status,
        std::optional<SubscriptionSettings> settings = std::nullopt);

private:
    struct Entry {
        ProfileInfo info;
        std::string revision;
        SubscriptionSettings subscription;
    };

    explicit ProfileStore(std::filesystem::path directory);
    const Entry* Find(std::string_view id) const;
    core::Result<void> Commit(std::vector<Entry> entries, std::string selected);
    core::Result<ProfileInfo> AddProfile(Entry entry, std::string_view bytes);
    core::Result<std::string> WriteCopy(std::string_view bytes);
    void RemoveCopy(std::string_view revision) const;

    std::filesystem::path directory_;
    std::string selected_{kBuiltInProfileId};
    std::vector<Entry> entries_;
};

}  // namespace clashyuka::runtime
