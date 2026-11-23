#include "clashyuka/runtime/profile_store.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/yaml.h>

#include "clashyuka/platform/platform_services.h"

namespace clashyuka::runtime {
namespace {

namespace fs = std::filesystem;
using core::ErrorKind;
using core::MakeError;
using nlohmann::json;

constexpr std::string_view kBuiltInName = "Built-in";
constexpr int kCatalogVersion = 1;
constexpr std::size_t kMaxCatalogBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxNameBytes = 240;
constexpr std::size_t kMaxYamlNodes = 200000;
constexpr std::size_t kMaxYamlDepth = 64;

core::Error Invalid(const char* message) {
    return MakeError(ErrorKind::InvalidConfig, message);
}

core::Error StoreError(const char* message) {
    return MakeError(ErrorKind::ConfigFailed, message);
}

std::string ToUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

std::string NameKey(std::string_view name) {
    std::string key(name);
    std::ranges::transform(key, key.begin(), LowerAscii);
    return key;
}

bool IsValidName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxNameBytes &&
           name.find_first_not_of(' ') != std::string_view::npos &&
           std::ranges::none_of(name, [](unsigned char ch) { return ch < 32 || ch == 127; });
}

bool IsWebUrl(std::string_view url, bool httpsOnly) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return false;
    }
    const auto scheme = NameKey(url.substr(0, schemeEnd));
    if ((scheme != "https" && (httpsOnly || scheme != "http")) ||
        std::ranges::any_of(url, [](unsigned char ch) { return ch <= 32 || ch == 127 || ch == '\\'; })) {
        return false;
    }
    url.remove_prefix(schemeEnd + 3);
    return !url.empty() && url.front() != '/' && url.front() != '?' && url.front() != '#';
}

bool IsValidSettings(const SubscriptionSettings& settings) {
    return settings.url.size() <= 8192 && IsWebUrl(settings.url, true) &&
           settings.refreshInterval.count() >= 0;
}

bool IsValidStatus(ProfileRefreshStatus status) {
    return status.outcome >= ProfileRefreshOutcome::Never &&
           status.outcome <= ProfileRefreshOutcome::Cancelled &&
           (status.outcome == ProfileRefreshOutcome::Never ? status.completedAt == 0
                                                            : status.completedAt > 0);
}

bool IsToken(std::string_view value) {
    return value.size() == 32 && value.find_first_not_of("0123456789abcdef") == value.npos;
}

std::string NewToken() {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device random;
    std::string token(32, '0');
    for (char& digit : token) {
        digit = kHex[random() % 16];
    }
    return token;
}

core::Result<std::string> ReadBounded(const fs::path& path, std::size_t maxBytes) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) {
        return StoreError("profile file is missing or is not a regular file");
    }
    const auto size = fs::file_size(path, error);
    if (error) {
        return StoreError("could not inspect the profile file");
    }
    if (size > maxBytes) {
        return Invalid("profile file exceeds the size limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return StoreError("could not open the profile file");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof() || input.bad()) {
        return StoreError("could not read the profile file or its size changed");
    }
    return bytes;
}

core::Result<fs::path> MakePrivateDirectory(const fs::path& parent) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const fs::path path = parent / NewToken();
        std::error_code error;
        if (fs::create_directory(path, error)) {
#ifndef _WIN32
            fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, error);
            if (error) {
                fs::remove(path, error);
                return StoreError("could not protect the profile storage directory");
            }
#endif
            return path;
        }
        if (error && error != std::errc::file_exists) {
            break;
        }
    }
    return StoreError("could not create a private profile storage directory");
}

bool WriteFile(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
#ifndef _WIN32
    // Catalog contains subscription credentials and moves out of the private directory.
    std::error_code error;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, error);
    if (error) {
        return false;
    }
#endif
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return !!output;
}

struct YamlLimit {};

// Bound parser events before allocating the node graph. The second pass also
// bounds walks through aliases and rejects recursive alias graphs.
class YamlBudget : public YAML::EventHandler {
public:
    void OnDocumentStart(const YAML::Mark&) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&, YAML::anchor_t) override { Count(); }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override { Count(); }
    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t,
                  const std::string&) override { Count(); }
    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t,
                         YAML::EmitterStyle::value) override { Enter(); }
    void OnSequenceEnd() override { --depth_; }
    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t,
                    YAML::EmitterStyle::value) override { Enter(); }
    void OnMapEnd() override { --depth_; }

private:
    void Count() {
        if (++nodes_ > kMaxYamlNodes) {
            throw YamlLimit{};
        }
    }
    void Enter() {
        Count();
        if (++depth_ > kMaxYamlDepth) {
            throw YamlLimit{};
        }
    }
    std::size_t nodes_ = 0;
    std::size_t depth_ = 0;
};

core::Result<void> CheckYamlTree(const YAML::Node& node, std::size_t depth, std::size_t& nodes) {
    if (++nodes > kMaxYamlNodes || depth > kMaxYamlDepth) {
        throw YamlLimit{};
    }
    const auto& tag = node.Tag();
    if (!tag.empty() && tag != "?" && tag != "!" && tag != "tag:yaml.org,2002:str" &&
        tag != "tag:yaml.org,2002:map" && tag != "tag:yaml.org,2002:seq" &&
        tag != "tag:yaml.org,2002:int" && tag != "tag:yaml.org,2002:bool" &&
        tag != "tag:yaml.org,2002:float" && tag != "tag:yaml.org,2002:null") {
        return Invalid("profile YAML contains an unsupported tag");
    }
    if (node.IsMap()) {
        std::unordered_set<std::string> keys;
        for (const auto& pair : node) {
            if (!pair.first.IsScalar() || pair.first.Scalar() == "<<" ||
                !keys.insert(pair.first.Scalar()).second) {
                return Invalid("profile YAML contains a non-scalar, duplicate, or merge key");
            }
            auto key = CheckYamlTree(pair.first, depth + 1, nodes);
            if (!key) {
                return key;
            }
            auto value = CheckYamlTree(pair.second, depth + 1, nodes);
            if (!value) {
                return value;
            }
        }
    } else if (node.IsSequence()) {
        for (const auto& item : node) {
            auto result = CheckYamlTree(item, depth + 1, nodes);
            if (!result) {
                return result;
            }
        }
    }
    return {};
}

bool IsSafeCachePath(std::string_view path) {
    if (path.empty()) {
        return true;
    }
    if (path.front() == '/' || path.front() == '\\' || path.find(':') != path.npos) {
        return false;
    }
    for (;;) {
        const auto end = path.find_first_of("/\\");
        const auto component = path.substr(0, end);
        if (component == ".." || (component != "." && !component.empty() &&
            (component.back() == ' ' || component.back() == '.'))) {
            return false;
        }
        if (end == path.npos) {
            return true;
        }
        path.remove_prefix(end + 1);
    }
}

core::Result<void> CheckDependencies(const YAML::Node& root) {
    for (const auto* key : {"proxy-providers", "rule-providers"}) {
        const auto providers = root[key];
        if (!providers || providers.IsNull()) {
            continue;
        }
        if (!providers.IsMap()) {
            return Invalid("profile providers must be a mapping");
        }
        for (const auto& pair : providers) {
            const YAML::Node provider = pair.second;
            if (!provider.IsMap()) {
                return Invalid("profile provider must be a mapping");
            }
            const auto type = provider["type"];
            if (type && type.IsScalar() && type.Scalar() == "inline") {
                continue;
            }
            const auto url = provider["url"];
            const auto path = provider["path"];
            if (!type || !type.IsScalar() || type.Scalar() != "http" || !url || !url.IsScalar() ||
                !IsWebUrl(url.Scalar(), false) ||
                (path && !path.IsNull() && (!path.IsScalar() || !IsSafeCachePath(path.Scalar())))) {
                return Invalid("profile depends on an external local file or unsupported provider");
            }
        }
    }
    const auto geox = root["geox-url"];
    if (geox && !geox.IsNull()) {
        if (!geox.IsMap()) {
            return Invalid("profile geox-url must be a mapping");
        }
        for (const auto& pair : geox) {
            if (!pair.second.IsScalar() || !IsWebUrl(pair.second.Scalar(), false)) {
                return Invalid("profile depends on an external local geodata file");
            }
        }
    }
    const auto ui = root["external-ui"];
    if (ui && !ui.IsNull() && (!ui.IsScalar() || !ui.Scalar().empty())) {
        return Invalid("profile depends on an external local UI directory");
    }
    // Mihomo accepts inline PEM or a local path for TLS certificates and keys.
    const auto checkCredentials = [](const YAML::Node& mapping) -> bool {
        if (!mapping || !mapping.IsMap()) {
            return true;
        }
        for (const auto* key : {"certificate", "private-key"}) {
            const auto field = mapping[key];
            if (field && !field.IsNull() &&
                (!field.IsScalar() || (!field.Scalar().empty() &&
                                       !field.Scalar().starts_with("-----BEGIN ")))) {
                return false;
            }
        }
        return true;
    };
    if (!checkCredentials(root["tls"])) {
        return Invalid("profile depends on an external local TLS file");
    }
    for (const auto* key : {"listeners", "proxies"}) {
        const auto items = root[key];
        if (items && items.IsSequence()) {
            for (const auto& item : items) {
                if (!checkCredentials(item)) {
                    return Invalid("profile depends on an external local TLS file");
                }
            }
        }
    }
    return {};
}

}  // namespace

core::Result<void> ValidateProfileYaml(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > kMaxProfileBytes) {
        return Invalid("profile YAML is empty or exceeds the size limit");
    }
    // yaml-cpp may treat an embedded NUL as EOF. Don't accept hidden trailing bytes.
    if (bytes.find('\0') != bytes.npos) {
        return Invalid("profile YAML contains a NUL byte; use UTF-8 YAML");
    }
    try {
        const std::string text(bytes);
        std::istringstream stream(text);
        YAML::Parser parser(stream);
        YamlBudget budget;
        if (!parser.HandleNextDocument(budget) || parser.HandleNextDocument(budget)) {
            return Invalid("profile must contain exactly one YAML document");
        }
        const YAML::Node root = YAML::Load(text);
        if (!root.IsMap()) {
            return Invalid("profile YAML root must be a mapping");
        }
        std::size_t nodes = 0;
        auto shape = CheckYamlTree(root, 0, nodes);
        return shape ? CheckDependencies(root) : shape;
    } catch (const YamlLimit&) {
        return Invalid("profile YAML exceeds the nesting or node limit");
    } catch (const YAML::Exception&) {
        // Parser diagnostics may embed YAML scalar values, including credentials.
        return Invalid("profile is not valid YAML");
    }
}

ProfileStore::ProfileStore(fs::path directory) : directory_(std::move(directory)) {}

core::Result<std::unique_ptr<ProfileStore>> ProfileStore::Open(const fs::path& userDataDirectory) {
    if (userDataDirectory.empty()) {
        return StoreError("profile store requires a user-data directory");
    }
    std::error_code error;
    auto directory = fs::absolute(userDataDirectory, error);
    if (error) {
        return StoreError("could not resolve the profile store directory");
    }
    auto store = std::unique_ptr<ProfileStore>(new ProfileStore(directory / "profiles"));
    const auto catalog = store->directory_ / "catalog.json";
    const auto state = fs::symlink_status(catalog, error);
    if (state.type() == fs::file_type::not_found) {
        return store;
    }
    if (error || state.type() != fs::file_type::regular) {
        return StoreError("could not inspect the profile catalog");
    }
    const auto bytes = ReadBounded(catalog, kMaxCatalogBytes);
    if (!bytes) {
        return bytes.error();
    }
    try {
        // A catalog produced by Commit has at most four enclosing containers.
        const auto root = json::parse(bytes.value(), [](int depth, json::parse_event_t, json&) {
            if (depth > 8) {
                throw YamlLimit{};
            }
            return true;
        });
        if (root.at("version") != kCatalogVersion || !root.at("entries").is_array()) {
            return StoreError("unsupported profile catalog format");
        }
        store->selected_ = root.at("selected").get<std::string>();
        // Keep a missing selection intact so activation can surface the error.
        if (store->selected_ != kBuiltInProfileId && !IsToken(store->selected_)) {
            return StoreError("profile catalog has an invalid selection");
        }
        std::unordered_set<std::string> ids, revisions, names{NameKey(kBuiltInName)};
        for (const auto& row : root.at("entries")) {
            Entry entry;
            entry.info.id = row.at("id").get<std::string>();
            entry.info.name = row.at("name").get<std::string>();
            entry.revision = row.at("revision").get<std::string>();
            if (!IsToken(entry.info.id) || !IsToken(entry.revision) ||
                !IsValidName(entry.info.name) || !ids.insert(entry.info.id).second ||
                !revisions.insert(entry.revision).second ||
                !names.insert(NameKey(entry.info.name)).second) {
                return StoreError("profile catalog contains an invalid or duplicate entry");
            }
            if (row.at("kind") == "local") {
                entry.info.kind = ProfileKind::Local;
            } else if (row.at("kind") == "subscription") {
                entry.info.kind = ProfileKind::Subscription;
                entry.subscription.url = row.at("url").get<std::string>();
                entry.subscription.refreshInterval = std::chrono::seconds(
                    row.at("intervalSeconds").get<long long>());
                const auto outcome = row.at("outcome").get<int>();
                entry.info.refresh = {static_cast<ProfileRefreshOutcome>(outcome),
                                      row.at("completedAt").get<long long>()};
                if (!IsValidSettings(entry.subscription) || !IsValidStatus(entry.info.refresh)) {
                    return StoreError("profile catalog contains invalid subscription metadata");
                }
            } else {
                return StoreError("profile catalog contains an unsupported profile type");
            }
            store->entries_.push_back(std::move(entry));
        }
        return store;
    } catch (const json::exception&) {
        return StoreError("profile catalog is not valid");
    } catch (const YamlLimit&) {
        return StoreError("profile catalog exceeds the nesting limit");
    }
}

std::vector<ProfileInfo> ProfileStore::ListProfiles() const {
    std::vector<ProfileInfo> profiles{{std::string(kBuiltInProfileId), std::string(kBuiltInName),
                                       ProfileKind::BuiltIn, {}}};
    profiles.reserve(entries_.size() + 1);
    for (const auto& entry : entries_) {
        profiles.push_back(entry.info);
    }
    return profiles;
}

std::string ProfileStore::SelectedProfileId() const { return selected_; }

const ProfileStore::Entry* ProfileStore::Find(std::string_view id) const {
    const auto it = std::ranges::find_if(entries_, [id](const Entry& entry) {
        return entry.info.id == id;
    });
    return it == entries_.end() ? nullptr : &*it;
}

core::Result<void> ProfileStore::SelectProfile(std::string_view id) {
    if (id != kBuiltInProfileId && !Find(id)) {
        return Invalid("profile was not found");
    }
    return id == selected_ ? core::Result<void>{} : Commit(entries_, std::string(id));
}

core::Result<std::string> ProfileStore::LoadProfileBytes(std::string_view id) const {
    if (id == kBuiltInProfileId) {
        // Application-managed runtime settings are applied by the activation layer.
        return std::string("{}\n");
    }
    const auto* entry = Find(id);
    if (!entry) {
        return Invalid("profile was not found");
    }
    auto bytes = ReadBounded(directory_ / "copies" / entry->revision / "config.yaml", kMaxProfileBytes);
    if (!bytes) {
        return bytes;
    }
    const auto valid = ValidateProfileYaml(bytes.value());
    return valid ? std::move(bytes) : core::Result<std::string>(valid.error());
}

core::Result<void> ProfileStore::Commit(std::vector<Entry> entries, std::string selected) {
    std::string text;
    try {
        auto rows = json::array();
        for (const auto& entry : entries) {
            json row{{"id", entry.info.id}, {"name", entry.info.name}, {"revision", entry.revision},
                     {"kind", entry.info.kind == ProfileKind::Local ? "local" : "subscription"}};
            if (entry.info.kind == ProfileKind::Subscription) {
                row["url"] = entry.subscription.url;
                row["intervalSeconds"] = entry.subscription.refreshInterval.count();
                row["outcome"] = static_cast<int>(entry.info.refresh.outcome);
                row["completedAt"] = entry.info.refresh.completedAt;
            }
            rows.push_back(std::move(row));
        }
        text = json{{"version", kCatalogVersion}, {"selected", selected},
                    {"entries", std::move(rows)}}.dump();
    } catch (const json::exception&) {
        return Invalid("profile metadata must be valid UTF-8");
    }
    if (text.size() > kMaxCatalogBytes) {
        return StoreError("profile catalog exceeds the size limit");
    }
    std::error_code error;
    fs::create_directories(directory_, error);
    if (error) {
        return StoreError("could not create the profile store directory");
    }
    auto staging = MakePrivateDirectory(directory_);
    if (!staging) {
        return staging.error();
    }
    const auto temporary = staging.value() / "catalog.json";
    const auto destination = directory_ / "catalog.json";
    const bool committed = WriteFile(temporary, text) &&
        platform::ReplaceFileAtomically(ToUtf8(temporary), ToUtf8(destination), nullptr);
    fs::remove_all(staging.value(), error);
    if (!committed) {
        return StoreError("could not write or replace the profile catalog");
    }
    entries_.swap(entries);
    selected_.swap(selected);
    return {};
}

core::Result<std::string> ProfileStore::WriteCopy(std::string_view bytes) {
    const auto copies = directory_ / "copies";
    std::error_code error;
    fs::create_directories(copies, error);
    if (error) {
        return StoreError("could not create the profile copies directory");
    }
    auto revisionDirectory = MakePrivateDirectory(copies);
    if (!revisionDirectory) {
        return revisionDirectory.error();
    }
    if (!WriteFile(revisionDirectory.value() / "config.yaml", bytes)) {
        fs::remove_all(revisionDirectory.value(), error);
        return StoreError("could not write the profile copy");
    }
    return ToUtf8(revisionDirectory.value().filename());
}

void ProfileStore::RemoveCopy(std::string_view revision) const {
    std::error_code ignored;
    fs::remove_all(directory_ / "copies" / revision, ignored);
}

core::Result<ProfileInfo> ProfileStore::AddProfile(Entry entry, std::string_view bytes) {
    const auto valid = ValidateProfileYaml(bytes);
    if (!valid) {
        return valid.error();
    }
    if (!IsValidName(entry.info.name)) {
        return Invalid("profile name is empty, too long, or contains control characters");
    }
    std::unordered_set<std::string> names{NameKey(kBuiltInName)};
    for (const auto& existing : entries_) {
        names.insert(NameKey(existing.info.name));
    }
    const std::string base = entry.info.name;
    for (std::size_t suffix = 2; names.contains(NameKey(entry.info.name)); ++suffix) {
        const auto tail = " (" + std::to_string(suffix) + ")";
        auto length = std::min(base.size(), kMaxNameBytes - tail.size());
        while (length < base.size() && (static_cast<unsigned char>(base[length]) & 0xc0) == 0x80) {
            --length;
        }
        entry.info.name = base.substr(0, length) + tail;
    }
    do {
        entry.info.id = NewToken();
    } while (entry.info.id == selected_ || Find(entry.info.id));
    auto next = entries_;
    auto revision = WriteCopy(bytes);
    if (!revision) {
        return revision.error();
    }
    entry.revision = revision.Take();
    next.push_back(entry);
    auto written = Commit(std::move(next), selected_);
    if (!written) {
        RemoveCopy(entry.revision);
        return written.error();
    }
    return entry.info;
}

core::Result<ProfileInfo> ProfileStore::ImportLocal(const fs::path& source, std::string_view name) {
    auto bytes = ReadBounded(source, kMaxProfileBytes);
    if (!bytes) {
        return bytes.error();
    }
    Entry entry;
    entry.info.name = name.empty() ? ToUtf8(source.stem()) : std::string(name);
    return AddProfile(std::move(entry), bytes.value());
}

core::Result<ProfileInfo> ProfileStore::AddSubscription(std::string_view name,
                                                        const SubscriptionSettings& settings,
                                                        std::string_view bytes,
                                                        ProfileRefreshStatus status) {
    if (!IsValidSettings(settings) || !IsValidStatus(status) ||
        (status.outcome != ProfileRefreshOutcome::Never &&
         status.outcome != ProfileRefreshOutcome::Succeeded)) {
        return Invalid("subscription requires an HTTPS URL and valid refresh metadata");
    }
    Entry entry;
    entry.info.name = name;
    entry.info.kind = ProfileKind::Subscription;
    entry.info.refresh = status;
    entry.subscription = settings;
    return AddProfile(std::move(entry), bytes);
}

core::Result<void> ProfileStore::RenameProfile(std::string_view id, std::string_view name) {
    const auto* entry = Find(id);
    if (!entry) {
        return Invalid("profile was not found or is built-in");
    }
    if (!IsValidName(name)) {
        return Invalid("profile name is empty, too long, or contains control characters");
    }
    const auto key = NameKey(name);
    if (key == NameKey(kBuiltInName) || std::ranges::any_of(entries_, [&](const Entry& other) {
        return other.info.id != id && NameKey(other.info.name) == key;
    })) {
        return Invalid("profile name is already in use");
    }
    auto next = entries_;
    next[entry - entries_.data()].info.name = name;
    return Commit(std::move(next), selected_);
}

core::Result<void> ProfileStore::DeleteProfile(std::string_view id, std::string_view runningProfileId) {
    const auto* entry = Find(id);
    if (!entry) {
        return Invalid("profile was not found or is built-in");
    }
    if (id == selected_ || id == runningProfileId) {
        return Invalid("switch away from the selected or running profile before deleting it");
    }
    const auto revision = entry->revision;
    auto next = entries_;
    next.erase(next.begin() + (entry - entries_.data()));
    auto written = Commit(std::move(next), selected_);
    if (written) {
        RemoveCopy(revision);
    }
    return written;
}

core::Result<SubscriptionSettings> ProfileStore::GetSubscriptionSettings(std::string_view id) const {
    const auto* entry = Find(id);
    if (!entry || entry->info.kind != ProfileKind::Subscription) {
        return Invalid("subscription profile was not found");
    }
    return entry->subscription;
}

core::Result<void> ProfileStore::UpdateSubscriptionSettings(std::string_view id,
                                                            const SubscriptionSettings& settings) {
    const auto* entry = Find(id);
    if (!entry || entry->info.kind != ProfileKind::Subscription) {
        return Invalid("subscription profile was not found");
    }
    if (!IsValidSettings(settings)) {
        return Invalid("subscription requires an HTTPS URL and a nonnegative refresh interval");
    }
    auto next = entries_;
    next[entry - entries_.data()].subscription = settings;
    return Commit(std::move(next), selected_);
}

core::Result<void> ProfileStore::RecordSubscriptionStatus(std::string_view id,
                                                          ProfileRefreshStatus status) {
    const auto* entry = Find(id);
    if (!entry || entry->info.kind != ProfileKind::Subscription) {
        return Invalid("subscription profile was not found");
    }
    if (!IsValidStatus(status) || status.outcome == ProfileRefreshOutcome::Succeeded ||
        status.outcome == ProfileRefreshOutcome::Never) {
        return Invalid("status-only updates require a completed unsuccessful refresh");
    }
    auto next = entries_;
    next[entry - entries_.data()].info.refresh = status;
    return Commit(std::move(next), selected_);
}

core::Result<void> ProfileStore::ReplaceSubscriptionContent(
    std::string_view id, std::string_view bytes, ProfileRefreshStatus status,
    std::optional<SubscriptionSettings> settings) {
    const auto* entry = Find(id);
    if (!entry || entry->info.kind != ProfileKind::Subscription) {
        return Invalid("subscription profile was not found");
    }
    if (status.outcome != ProfileRefreshOutcome::Succeeded || !IsValidStatus(status) ||
        (settings && !IsValidSettings(*settings))) {
        return Invalid("replacement requires a successful refresh and valid subscription metadata");
    }
    const auto valid = ValidateProfileYaml(bytes);
    if (!valid) {
        return valid;
    }
    const auto previousRevision = entry->revision;
    auto next = entries_;
    auto& updated = next[entry - entries_.data()];
    updated.info.refresh = status;
    if (settings) {
        updated.subscription = std::move(*settings);
    }
    auto revision = WriteCopy(bytes);
    if (!revision) {
        return revision.error();
    }
    updated.revision = revision.value();
    auto written = Commit(std::move(next), selected_);
    // The catalog is the commit point. Only reclaim the old copy after publish.
    RemoveCopy(written ? previousRevision : revision.value());
    return written;
}

}  // namespace clashyuka::runtime
