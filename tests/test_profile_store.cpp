#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "clashyuka/runtime/profile_store.h"

using namespace clashyuka::runtime;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr auto kOriginal = "# original\r\nrules: [MATCH,DIRECT]\r\n";
constexpr auto kUpdated = "rules: []\n";

struct TempDir {
    fs::path path = fs::temp_directory_path() /
        ("clashyuka-profile-" + std::to_string(std::random_device{}()));

    TempDir() { REQUIRE(fs::create_directory(path)); }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void Write(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(!!output);
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(!!input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::unique_ptr<ProfileStore> Open(const fs::path& directory) {
    auto opened = ProfileStore::Open(directory);
    REQUIRE_MESSAGE(opened, opened.error().message);
    return opened.Take();
}

TEST_CASE("built-in profile is always selectable and immutable") {
    TempDir dir;
    auto store = Open(dir.path);
    const auto profiles = store->ListProfiles();
    REQUIRE(profiles.size() == 1);
    CHECK(profiles.front().id == kBuiltInProfileId);
    CHECK(profiles.front().kind == ProfileKind::BuiltIn);
    CHECK(store->SelectedProfileId() == kBuiltInProfileId);
    auto loaded = store->LoadProfileBytes(kBuiltInProfileId);
    REQUIRE(loaded);
    CHECK(ValidateProfileYaml(loaded.value()));
    CHECK_FALSE(store->RenameProfile(kBuiltInProfileId, "Other"));
    CHECK_FALSE(store->DeleteProfile(kBuiltInProfileId, {}));
    CHECK_FALSE(store->SelectProfile("missing"));
    CHECK_FALSE(store->GetSubscriptionSettings(kBuiltInProfileId));
    CHECK_FALSE(fs::exists(dir.path / "profiles"));
}

TEST_CASE("local imports preserve source and selection and persist independent Unicode copies") {
    TempDir dir;
    const auto userData = dir.path / u8"\u7528\u6237\u8d44\u6599";
    const auto source = dir.path / u8"\u914d\u7f6e.yaml";
    Write(source, kOriginal);
    fs::create_directory(userData);
    Write(userData / "config.yaml", "legacy user config");
    auto store = Open(userData);
    auto first = store->ImportLocal(source);
    REQUIRE_MESSAGE(first, first.error().message);
    CHECK(first.value().name == "\u914d\u7f6e");
    CHECK(first.value().kind == ProfileKind::Local);
    CHECK(store->SelectedProfileId() == kBuiltInProfileId);
    CHECK(Read(source) == kOriginal);
    Write(source, kUpdated);
    auto second = store->ImportLocal(source);
    REQUIRE(second);
    CHECK(second.value().name == "\u914d\u7f6e (2)");
    CHECK(second.value().id != first.value().id);
    auto reserved = store->ImportLocal(source, "built-in");
    REQUIRE(reserved);
    CHECK(reserved.value().name == "built-in (2)");
    CHECK_FALSE(store->RenameProfile(first.value().id, second.value().name));
    CHECK(store->RenameProfile(second.value().id, "\u5df2\u91cd\u547d\u540d"));
    CHECK(store->SelectProfile(first.value().id));
    CHECK(Read(source) == kUpdated);
    REQUIRE(fs::remove(source));
    CHECK(Read(userData / "config.yaml") == "legacy user config");

    store.reset();
    store = Open(userData);
    CHECK(store->SelectedProfileId() == first.value().id);
    REQUIRE(store->ListProfiles().size() == 4);
    CHECK(store->ListProfiles()[2].name == "\u5df2\u91cd\u547d\u540d");
    CHECK(store->LoadProfileBytes(first.value().id).value() == kOriginal);
    CHECK(store->LoadProfileBytes(second.value().id).value() == kUpdated);
    CHECK_FALSE(store->DeleteProfile(first.value().id, {}));
    CHECK_FALSE(store->DeleteProfile(second.value().id, second.value().id));
    CHECK(store->SelectProfile(kBuiltInProfileId));
    CHECK(store->DeleteProfile(first.value().id, second.value().id));
    CHECK_FALSE(store->LoadProfileBytes(first.value().id));
    store.reset();
    CHECK(Open(userData)->ListProfiles().size() == 3);
}

TEST_CASE("bounded YAML validation allows self-contained and remote provider mappings") {
    CHECK(ValidateProfileYaml("{}\n"));
    CHECK(ValidateProfileYaml("common: &p {type: http, url: https://example.test/rules}\n"
                              "rule-providers: {a: *p}\n"));
    CHECK(ValidateProfileYaml("proxy-providers:\n  p:\n    type: http\n"
                              "    url: https://example.test/p\n    path: ./providers/p.yaml\n"));
    CHECK(ValidateProfileYaml("proxy-providers: {p: {type: inline, payload: []}}\n"));
    for (const auto* bad : {"", "x: [", "[a, b]", "---\na: b\n---\nc: d\n",
                            "a: 1\na: 2\n", "a: &a [*a]\n", "include: !include local.yml\n",
                            "proxy-providers: {p: {type: file, path: proxies.yml}}\n",
                            "rule-providers: {p: {type: http, url: file:///local}}\n",
                            "rule-providers: {p: {type: http, url: 'https://example.test', path: ../a}}\n",
                            "tls: {certificate: certificate.pem}\n",
                            "listeners: [{private-key: C:/certs/key.pem}]\n",
                            "geox-url: {geoip: file:///data/geoip.dat}\n",
                            "external-ui: dashboard\n"}) {
        CAPTURE(bad);
        CHECK_FALSE(ValidateProfileYaml(bad));
    }
    CHECK_FALSE(ValidateProfileYaml(std::string(kMaxProfileBytes + 1, ' ')));
    CHECK_FALSE(ValidateProfileYaml("x: " + std::string(200, '[') + std::string(200, ']')));
    CHECK_FALSE(ValidateProfileYaml(std::string("a: b\0hidden", 11)));
}

TEST_CASE("failed imports and a blocked store do not change the catalog") {
    TempDir dir;
    auto store = Open(dir.path);
    const auto source = dir.path / "local.yaml";
    CHECK_FALSE(store->ImportLocal(source));
    Write(source, "invalid: [");
    CHECK_FALSE(store->ImportLocal(source));
    CHECK_FALSE(fs::exists(dir.path / "profiles"));
    Write(source, kOriginal);
    Write(dir.path / "profiles", "existing unrelated file");
    CHECK_FALSE(store->ImportLocal(source));
    CHECK(store->ListProfiles().size() == 1);
    CHECK(Read(dir.path / "profiles") == "existing unrelated file");
    CHECK(Read(source) == kOriginal);
}

TEST_CASE("subscription replacement persists settings and status with exact new bytes") {
    TempDir dir;
    auto store = Open(dir.path);
    const SubscriptionSettings original{"https://example.test/sub?token=private"};
    const SubscriptionSettings updated{"https://example.test/other?token=another", 3600s};
    const ProfileRefreshStatus success{ProfileRefreshOutcome::Succeeded, 100};
    CHECK_FALSE(store->AddSubscription("Bad", {"http://example.test"}, kOriginal));
    auto sub = store->AddSubscription("My subscription", original, kOriginal);
    REQUIRE_MESSAGE(sub, sub.error().message);
    const auto id = sub.value().id;
    CHECK(store->GetSubscriptionSettings(id).value().refreshInterval == 0s);
    CHECK(store->SelectedProfileId() == kBuiltInProfileId);
    REQUIRE(store->SelectProfile(id));
    auto invalid = store->ReplaceSubscriptionContent(id, "# private\nrules: [", success, updated);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().message.find("private") == std::string::npos);
    CHECK(store->GetSubscriptionSettings(id).value().url == original.url);
    CHECK(store->LoadProfileBytes(id).value() == kOriginal);
    REQUIRE(store->RecordSubscriptionStatus(id, {ProfileRefreshOutcome::NetworkFailed, 90}));
    CHECK(store->LoadProfileBytes(id).value() == kOriginal);
    CHECK(store->ListProfiles().back().refresh.outcome == ProfileRefreshOutcome::NetworkFailed);
    REQUIRE(store->ReplaceSubscriptionContent(id, kUpdated, success, updated));
    store.reset();
    store = Open(dir.path);
    CHECK(store->SelectedProfileId() == id);
    CHECK(store->LoadProfileBytes(id).value() == kUpdated);
    CHECK(store->GetSubscriptionSettings(id).value().url == updated.url);
    CHECK(store->GetSubscriptionSettings(id).value().refreshInterval == 3600s);
    CHECK(store->ListProfiles().back().refresh.outcome == ProfileRefreshOutcome::Succeeded);
    CHECK(store->ListProfiles().back().refresh.completedAt == 100);
    REQUIRE(store->UpdateSubscriptionSettings(id, {updated.url, 0s}));
    CHECK(store->LoadProfileBytes(id).value() == kUpdated);
    CHECK(store->ListProfiles().back().refresh.completedAt == 100);
}

TEST_CASE("catalog replacement failure leaves last good subscription and removes staged copy") {
    TempDir dir;
    auto store = Open(dir.path);
    const SubscriptionSettings original{"https://example.test/sub"};
    const auto sub = store->AddSubscription("My subscription", original, kOriginal,
                                            {ProfileRefreshOutcome::Succeeded, 50});
    REQUIRE(sub);
    const auto id = sub.value().id;
    const auto profiles = dir.path / "profiles";
    const auto catalog = profiles / "catalog.json";
    const auto backup = profiles / "catalog.backup";
    fs::rename(catalog, backup);
    REQUIRE(fs::create_directory(catalog));
    auto replaced = store->ReplaceSubscriptionContent(id, kUpdated,
        {ProfileRefreshOutcome::Succeeded, 100}, SubscriptionSettings{"https://example.test/new"});
    REQUIRE_FALSE(replaced);
    CHECK(store->LoadProfileBytes(id).value() == kOriginal);
    CHECK(store->GetSubscriptionSettings(id).value().url == original.url);
    CHECK(store->ListProfiles().back().refresh.completedAt == 50);
    CHECK(std::distance(fs::directory_iterator(profiles / "copies"), fs::directory_iterator()) == 1);
    CHECK(fs::remove(catalog));
    fs::rename(backup, catalog);
    store.reset();
    CHECK(Open(dir.path)->LoadProfileBytes(id).value() == kOriginal);
}

TEST_CASE("missing selected copy remains selected and is reported on load") {
    TempDir dir;
    const auto source = dir.path / "local.yaml";
    Write(source, kOriginal);
    auto store = Open(dir.path);
    auto imported = store->ImportLocal(source);
    REQUIRE(imported);
    REQUIRE(store->SelectProfile(imported.value().id));
    store.reset();
    REQUIRE(fs::remove_all(dir.path / "profiles" / "copies") > 0);
    store = Open(dir.path);
    CHECK(store->SelectedProfileId() == imported.value().id);
    CHECK_FALSE(store->LoadProfileBytes(store->SelectedProfileId()));
    CHECK(store->LoadProfileBytes(kBuiltInProfileId));
    CHECK(store->SelectProfile(kBuiltInProfileId));
}

}  // namespace
