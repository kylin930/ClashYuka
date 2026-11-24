#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "clashyuka/runtime/kernel_session.h"
#include "clashyuka/runtime/runtime_config.h"
#include "clashyuka/runtime/settings_store.h"

using namespace clashyuka;
using namespace clashyuka::runtime;
namespace fs = std::filesystem;

namespace {

std::string ToUtf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

fs::path FromUtf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

struct TempDir {
    fs::path path;

    TempDir() {
        std::random_device device;
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() /
               ("clashyuka-p22-" + std::to_string(device()) + "-" +
                std::to_string(counter.fetch_add(1)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    return text;
}

void WriteFile(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

template <typename Fn>
bool WaitUntil(Fn predicate, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// ---------------------------------------------------------------------------
// SettingsStore
// ---------------------------------------------------------------------------

TEST_CASE("settings are created with defaults on first run") {
    TempDir dir;
    const std::string path = ToUtf8(dir.path / "settings.json");

    const runtime::SettingsLoadResult first = runtime::LoadSettings(path);
    CHECK(first.firstRun);
    CHECK(first.settings.controllerHost == "127.0.0.1");
    CHECK(first.settings.controllerPort == 9090);
    CHECK(first.settings.mixedPort == 7897);
    CHECK(first.settings.secret.size() == 32);
    CHECK_FALSE(first.settings.autoStartKernel);
    CHECK_FALSE(first.settings.closeToTray);
    CHECK(first.settings.windowHotkey.empty());
    CHECK(fs::exists(FromUtf8(path)));

    const runtime::SettingsLoadResult second = runtime::LoadSettings(path);
    CHECK_FALSE(second.firstRun);
    CHECK(second.settings.secret == first.settings.secret);
}

TEST_CASE("settings round trip through JSON") {
    TempDir dir;
    const std::string path = ToUtf8(dir.path / "settings.json");

    runtime::AppSettings settings = runtime::DefaultSettings();
    settings.mihomoPath = "C:/tools/mihomo.exe";
    settings.controllerPort = 19090;
    settings.mixedPort = 17897;
    settings.autoStartKernel = true;
    settings.enableSystemProxy = true;

    const core::Result<void> saved = runtime::SaveSettings(path, settings);
    REQUIRE(saved.ok());

    const runtime::SettingsLoadResult loaded = runtime::LoadSettings(path);
    CHECK_FALSE(loaded.firstRun);
    CHECK(loaded.warning.empty());
    CHECK(loaded.settings.mihomoPath == settings.mihomoPath);
    CHECK(loaded.settings.controllerPort == 19090);
    CHECK(loaded.settings.mixedPort == 17897);
    CHECK(loaded.settings.autoStartKernel);
    CHECK(loaded.settings.enableSystemProxy);
    CHECK(loaded.settings.secret == settings.secret);
}

TEST_CASE("desktop settings round trip and invalid shortcuts preserve saved preferences") {
    TempDir dir;
    const auto path = ToUtf8(dir.path / "settings.json");
    auto settings = runtime::DefaultSettings();
    settings.closeToTray = true;
    settings.windowHotkey = "Ctrl+Alt+Y";
    REQUIRE(runtime::SaveSettings(path, settings).ok());
    settings.closeToTray = false;
    settings.windowHotkey = "Ctrl+Ctrl+Y";
    CHECK_FALSE(runtime::SaveSettings(path, settings).ok());
    const auto loaded = runtime::LoadSettings(path);
    CHECK(loaded.warning.empty());
    CHECK(loaded.settings.closeToTray);
    CHECK(loaded.settings.windowHotkey == "Ctrl+Alt+Y");
}

TEST_CASE("older desktop settings default off and malformed fields do not reset other settings") {
    TempDir dir;
    const auto file = dir.path / "settings.json";
    const auto path = ToUtf8(file);
    auto settings = runtime::DefaultSettings();
    settings.controllerPort = 19001;
    REQUIRE(runtime::SaveSettings(path, settings).ok());
    auto root = nlohmann::json::parse(ReadFile(file));
    root.erase("closeToTray");
    root.erase("windowHotkey");
    WriteFile(file, root.dump());
    const auto legacy = runtime::LoadSettings(path);
    CHECK(legacy.warning.empty());
    CHECK_FALSE(legacy.settings.closeToTray);
    CHECK(legacy.settings.windowHotkey.empty());
    root["closeToTray"] = "true";
    root["windowHotkey"] = "Y";
    WriteFile(file, root.dump());
    const auto malformed = runtime::LoadSettings(path);
    CHECK_FALSE(malformed.warning.empty());
    CHECK_FALSE(malformed.settings.closeToTray);
    CHECK(malformed.settings.windowHotkey.empty());
    CHECK(malformed.settings.controllerPort == 19001);
}

TEST_CASE("shortcut text is normalized and requires a control modifier and a letter or digit") {
    const auto shortcut = runtime::ParseWindowHotkey(" shift + aLt + ctrl + y ");
    REQUIRE(shortcut);
    CHECK(shortcut->ToString() == "Ctrl+Alt+Shift+Y");
    const auto disabled = runtime::ParseWindowHotkey("  ");
    REQUIRE(disabled);
    CHECK(disabled->ToString().empty());
    CHECK(runtime::ParseWindowHotkey("Win+7").has_value());
    for (const auto* bad : {"Y", "Shift+Y", "Ctrl++Y", "Ctrl+Y+", "Ctrl+F1", "Alt+?"}) {
        CAPTURE(bad);
        CHECK_FALSE(runtime::ParseWindowHotkey(bad));
    }
}

TEST_CASE("missing settings fields fall back to defaults") {
    TempDir dir;
    const fs::path file = dir.path / "settings.json";
    WriteFile(file, R"({"controllerPort": 19001})");

    const runtime::SettingsLoadResult loaded = runtime::LoadSettings(ToUtf8(file));
    CHECK(loaded.settings.controllerPort == 19001);
    CHECK(loaded.settings.controllerHost == "127.0.0.1");
    CHECK(loaded.settings.mixedPort == 7897);
    CHECK(loaded.settings.secret.size() == 32);
    CHECK_FALSE(loaded.warning.empty());
}

TEST_CASE("a corrupt settings file is preserved and defaults are used") {
    TempDir dir;
    const fs::path file = dir.path / "settings.json";
    const std::string broken = "{not json";
    WriteFile(file, broken);

    const runtime::SettingsLoadResult loaded = runtime::LoadSettings(ToUtf8(file));
    CHECK_FALSE(loaded.warning.empty());
    CHECK(loaded.settings.controllerPort == 9090);
    CHECK(ReadFile(file) == broken);
}

TEST_CASE("settings save failure is reported") {
    TempDir dir;
    const fs::path target = dir.path / "target-is-a-directory";
    fs::create_directories(target);

    const core::Result<void> saved =
        runtime::SaveSettings(ToUtf8(target), runtime::DefaultSettings());
    CHECK_FALSE(saved.ok());
    CHECK_FALSE(saved.error().message.empty());
}

TEST_CASE("settings do not overwrite unrelated files") {
    TempDir dir;
    const fs::path other = dir.path / "other.json";
    WriteFile(other, "keep me");

    const std::string path = ToUtf8(dir.path / "nested" / "settings.json");
    REQUIRE(runtime::SaveSettings(path, runtime::DefaultSettings()).ok());
    CHECK(ReadFile(other) == "keep me");
    CHECK(fs::exists(FromUtf8(path)));
}

TEST_CASE("settings validation rejects unsafe values") {
    runtime::AppSettings settings = runtime::DefaultSettings();
    settings.controllerHost = "0.0.0.0";
    CHECK_FALSE(runtime::ValidateSettings(settings).ok());

    settings = runtime::DefaultSettings();
    settings.controllerPort = 0;
    CHECK_FALSE(runtime::ValidateSettings(settings).ok());

    settings = runtime::DefaultSettings();
    settings.mixedPort = settings.controllerPort;
    CHECK_FALSE(runtime::ValidateSettings(settings).ok());

    settings = runtime::DefaultSettings();
    settings.secret = "line\nbreak";
    CHECK_FALSE(runtime::ValidateSettings(settings).ok());
}

// ---------------------------------------------------------------------------
// RuntimeConfig
// ---------------------------------------------------------------------------

TEST_CASE("runtime config is generated for loopback only") {
    TempDir dir;
    runtime::RuntimeConfigDefinition definition;
    definition.port = 19090;
    definition.mixedPort = 17897;
    definition.secret = "s3cr3t";

    const std::string path = ToUtf8(dir.path / "clashyuka-runtime.yaml");
    const core::Result<runtime::RuntimeConfigInfo> info =
        runtime::EnsureRuntimeConfig(path, definition);
    REQUIRE(info.ok());
    CHECK(info.value().generated);
    CHECK(info.value().port == 19090);
    CHECK(info.value().mixedPort == 17897);

    const std::string text = ReadFile(FromUtf8(path));
    CHECK(text.find("127.0.0.1:19090") != std::string::npos);
    CHECK(text.find("bind-address") != std::string::npos);
    CHECK(text.find("allow-lan") != std::string::npos);
    CHECK(text.find("0.0.0.0") == std::string::npos);
    CHECK(text.find("mixed-port") != std::string::npos);
    CHECK_FALSE(fs::exists(FromUtf8(path + ".tmp")));

    const YAML::Node root = YAML::LoadFile(path);
    CHECK(root["allow-lan"].as<bool>() == false);
    CHECK(root["bind-address"].as<std::string>() == "127.0.0.1");
    CHECK(root["secret"].as<std::string>() == "s3cr3t");
}

TEST_CASE("an existing user config is adopted without being overwritten") {
    TempDir dir;
    const fs::path file = dir.path / "user.yaml";
    const std::string original =
        "mixed-port: 7890\n"
        "external-controller: 127.0.0.1:19999\n"
        "secret: user-secret\n";
    WriteFile(file, original);

    runtime::RuntimeConfigDefinition definition;
    definition.port = 19090;
    const core::Result<runtime::RuntimeConfigInfo> info =
        runtime::EnsureRuntimeConfig(ToUtf8(file), definition);
    REQUIRE(info.ok());
    CHECK_FALSE(info.value().generated);
    CHECK(info.value().port == 19999);
    CHECK(info.value().secret == "user-secret");
    CHECK(info.value().mixedPort == 7890);
    CHECK(ReadFile(file) == original);
}

TEST_CASE("a user config without a mixed port does not inherit the app default") {
    TempDir dir;
    const fs::path file = dir.path / "user.yaml";
    const std::string original = "external-controller: 127.0.0.1:19999\n";
    WriteFile(file, original);

    const auto info = runtime::EnsureRuntimeConfig(ToUtf8(file), {});
    REQUIRE(info.ok());
    CHECK(info.value().mixedPort == 0);
    CHECK(ReadFile(file) == original);
}

TEST_CASE("an app owned runtime config is regenerated when settings change") {
    TempDir dir;
    const std::string path = ToUtf8(dir.path / "clashyuka-runtime.yaml");
    runtime::RuntimeConfigDefinition definition;
    definition.port = 19090;
    definition.secret = "first";

    REQUIRE(runtime::EnsureRuntimeConfig(path, definition).ok());
    definition.port = 19091;
    definition.secret = "second";
    const core::Result<runtime::RuntimeConfigInfo> regenerated =
        runtime::EnsureRuntimeConfig(path, definition);
    REQUIRE(regenerated.ok());
    CHECK(regenerated.value().generated);
    CHECK(regenerated.value().port == 19091);

    const std::string text = ReadFile(FromUtf8(path));
    CHECK(text.find("127.0.0.1:19091") != std::string::npos);
    CHECK(text.find("second") != std::string::npos);
}

TEST_CASE("invalid runtime config values are rejected before writing") {
    TempDir dir;
    const std::string path = ToUtf8(dir.path / "clashyuka-runtime.yaml");
    runtime::RuntimeConfigDefinition definition;
    definition.port = 0;

    const core::Result<runtime::RuntimeConfigInfo> info =
        runtime::EnsureRuntimeConfig(path, definition);
    CHECK_FALSE(info.ok());
    CHECK(info.error().kind == core::ErrorKind::InvalidConfig);
    CHECK_FALSE(fs::exists(FromUtf8(path)));
}

TEST_CASE("runtime config write failures are reported") {
    TempDir dir;
    const fs::path blocker = dir.path / "blocked";
    WriteFile(blocker, "file");

    runtime::RuntimeConfigDefinition definition;
    const core::Result<runtime::RuntimeConfigInfo> info =
        runtime::EnsureRuntimeConfig(ToUtf8(blocker / "runtime.yaml"), definition);
    CHECK_FALSE(info.ok());
    CHECK(info.error().kind == core::ErrorKind::ConfigFailed);
}

TEST_CASE("runtime config secrets are escaped as YAML scalars") {
    TempDir dir;
    const std::string path = ToUtf8(dir.path / "clashyuka-runtime.yaml");
    runtime::RuntimeConfigDefinition definition;
    definition.secret = "a:b # c\"d";

    REQUIRE(runtime::EnsureRuntimeConfig(path, definition).ok());
    const YAML::Node root = YAML::LoadFile(path);
    CHECK(root["secret"].as<std::string>() == definition.secret);
}

TEST_CASE("a user config without an external controller is rejected") {
    TempDir dir;
    const fs::path file = dir.path / "user.yaml";
    WriteFile(file, "mixed-port: 7890\n");

    const core::Result<runtime::RuntimeConfigInfo> info =
        runtime::EnsureRuntimeConfig(ToUtf8(file), runtime::RuntimeConfigDefinition{});
    CHECK_FALSE(info.ok());
    CHECK(info.error().kind == core::ErrorKind::ConfigFailed);
}

// ---------------------------------------------------------------------------
// KernelSession fakes
// ---------------------------------------------------------------------------

struct FakeKernel {
    mutable std::mutex mutex;
    std::atomic<int> processStarts{0};
    std::atomic<int> processStops{0};
    std::atomic<int> preflightStarts{0};
    std::atomic<int> preflightStops{0};
    std::atomic<int> preflightCode{0}; // Negative simulates a hung check.
    std::atomic<bool> capturedImportedOutput{false};
    std::vector<std::string> liveConfigs;
    std::atomic<int> apiCalls{0};
    std::atomic<int> trafficConnects{0};
    std::atomic<int> connectionConnects{0};
    std::atomic<int> eventDisconnects{0};

    bool running = false;
    bool processStartResult = true;
    int exitAfterStartCode = -1;
    int apiFailuresBeforeSuccess = 0;
    int apiDelayMs = 0;
    core::ErrorKind apiFailKind = core::ErrorKind::ConnectionFailed;
    bool trafficConnectFails = false;
    bool connectionConnectFails = false;

    std::function<void(int)> exitHandler;
    KernelEventDriver::TrafficHandler trafficHandler;
    KernelEventDriver::ConnectionHandler connectionHandler;
    KernelEventDriver::LogHandler logHandler;
    KernelEventDriver::ErrorHandler connectionError;
    std::atomic<int> closeCalls{0};
    std::atomic<bool> blockClose{false};
    std::atomic<bool> failClose{false};
    std::string lastClosedId;
    std::vector<std::string> rulesRequests;
    std::atomic<int> providerUpdates{0};
    std::atomic<bool> blockProviderUpdate{false};
    std::atomic<bool> failProviderUpdate{false};
    std::atomic<bool> failProviderRead{false};
    std::atomic<int> ruleCount{1};

    void RecordRulesRequest(std::string request) {
        std::lock_guard<std::mutex> lock(mutex);
        rulesRequests.push_back(std::move(request));
    }

    std::vector<std::string> RulesRequests() const {
        std::lock_guard<std::mutex> lock(mutex);
        return rulesRequests;
    }
    mihomo::MihomoLaunchOptions lastLaunch;

    mihomo::MihomoLaunchOptions LastLaunch() const {
        std::lock_guard<std::mutex> lock(mutex);
        return lastLaunch;
    }

    KernelEventDriver::TrafficHandler CurrentTrafficHandler() const {
        std::lock_guard<std::mutex> lock(mutex);
        return trafficHandler;
    }

    KernelEventDriver::ConnectionHandler CurrentConnectionHandler() const {
        std::lock_guard<std::mutex> lock(mutex);
        return connectionHandler;
    }

    void SimulateExit(int code) {
        std::function<void(int)> handler;
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
            handler = exitHandler;
        }
        if (handler) {
            handler(code);
        }
    }

    void ConfigureApiFailures(int failures, int delayMs,
                              core::ErrorKind kind = core::ErrorKind::ConnectionFailed) {
        std::lock_guard<std::mutex> lock(mutex);
        apiFailuresBeforeSuccess = failures;
        apiDelayMs = delayMs;
        apiFailKind = kind;
    }

    void ConfigureEventFailures(bool traffic, bool connections) {
        std::lock_guard<std::mutex> lock(mutex);
        trafficConnectFails = traffic;
        connectionConnectFails = connections;
    }

    void ConfigureExitAfterStart(int code) {
        std::lock_guard<std::mutex> lock(mutex);
        exitAfterStartCode = code;
    }
};

class FakeProcessDriver final : public runtime::KernelProcessDriver {
public:
    explicit FakeProcessDriver(std::shared_ptr<FakeKernel> fake) : fake_(std::move(fake)) {}

    bool Start(const mihomo::MihomoLaunchOptions& options,
               mihomo::MihomoProcessHandlers handlers) override {
        preflight_ = !options.arguments.empty() && options.arguments[0] == "-t";
        if (preflight_) {
            fake_->preflightStarts.fetch_add(1);
            code_ = fake_->preflightCode.load();
            if (handlers.onStdoutLine || handlers.onStderrLine) {
                fake_->capturedImportedOutput.store(true);
            }
            return true;
        }
        bool result = true;
        int exitCode = -1;
        {
            std::lock_guard<std::mutex> lock(fake_->mutex);
            fake_->processStarts.fetch_add(1);
            fake_->lastLaunch = options;
            const auto path = FromUtf8(options.arguments.back());
            fake_->liveConfigs.push_back(ReadFile(path));
            if (options.arguments[1] == ToUtf8(path.parent_path()) &&
                (handlers.onStdoutLine || handlers.onStderrLine)) {
                fake_->capturedImportedOutput.store(true);
            }
            result = fake_->processStartResult;
            fake_->running = result;
            fake_->exitHandler = handlers.onExited;
            exitCode = fake_->exitAfterStartCode;
        }
        if (result && exitCode >= 0 && handlers.onExited) {
            handlers.onExited(exitCode);
        }
        return result;
    }

    void Stop() override {
        if (preflight_) {
            fake_->preflightStops.fetch_add(1);
            return;
        }
        std::lock_guard<std::mutex> lock(fake_->mutex);
        fake_->processStops.fetch_add(1);
        fake_->running = false;
        fake_->exitHandler = nullptr;
    }

    bool IsRunning() const override {
        std::lock_guard<std::mutex> lock(fake_->mutex);
        return fake_->running;
    }

    std::optional<int> ExitCode() const override {
        return preflight_ && code_ >= 0 ? std::optional<int>{code_} : std::nullopt;
    }

private:
    std::shared_ptr<FakeKernel> fake_;
    bool preflight_ = false;
    int code_ = -1;
};

class FakeApiDriver final : public runtime::KernelApiDriver {
public:
    explicit FakeApiDriver(std::shared_ptr<FakeKernel> fake) : fake_(std::move(fake)) {}

    core::Result<mihomo::MihomoVersion> GetVersion() override {
        int delay = 0;
        bool fail = false;
        core::ErrorKind kind = core::ErrorKind::ConnectionFailed;
        {
            std::lock_guard<std::mutex> lock(fake_->mutex);
            const int call = fake_->apiCalls.fetch_add(1) + 1;
            (void)call;
            delay = fake_->apiDelayMs;
            if (fake_->apiFailuresBeforeSuccess > 0) {
                --fake_->apiFailuresBeforeSuccess;
                fail = true;
                kind = fake_->apiFailKind;
            }
        }
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        if (fail) {
            return core::MakeError(kind, "simulated API failure");
        }
        return mihomo::MihomoVersion{"v1.19.0-test", true};
    }

    core::Result<void> CloseConnection(std::string_view id) override {
        {
            std::lock_guard<std::mutex> lock(fake_->mutex);
            fake_->lastClosedId = id;
        }
        ++fake_->closeCalls;
        WaitUntil([this] { return !fake_->blockClose.load(); });
        if (fake_->failClose.load()) {
            return core::MakeError(core::ErrorKind::HttpStatus, "private response text", 404);
        }
        return {};
    }

    core::Result<mihomo::RuleCollection> GetRules() override {
        fake_->RecordRulesRequest("rules");
        return mihomo::RuleCollection(static_cast<std::size_t>(fake_->ruleCount.load()));
    }

    core::Result<mihomo::RuleProviderCollection> GetRuleProviders() override {
        fake_->RecordRulesRequest("providers");
        if (fake_->failProviderRead) {
            return core::MakeError(core::ErrorKind::ConnectionFailed, "simulated provider read failure");
        }
        return mihomo::RuleProviderCollection{{{"remote", "Rule", "domain", "HTTP", "",
                                                fake_->ruleCount.load()},
                                               {"inline", "Rule", "domain", "Inline", "", 0}}};
    }

    core::Result<void> UpdateRuleProvider(std::string_view name) override {
        fake_->RecordRulesRequest("update " + std::string(name));
        ++fake_->providerUpdates;
        WaitUntil([this] { return !fake_->blockProviderUpdate.load(); });
        if (fake_->failProviderUpdate) {
            return core::MakeError(core::ErrorKind::HttpStatus, "simulated update failure");
        }
        ++fake_->ruleCount;
        return {};
    }

private:
    std::shared_ptr<FakeKernel> fake_;
};

class FakeEventDriver final : public runtime::KernelEventDriver {
public:
    explicit FakeEventDriver(std::shared_ptr<FakeKernel> fake) : fake_(std::move(fake)) {}

    core::Result<void> ConnectTraffic(TrafficHandler handler, ErrorHandler onError) override {
        std::lock_guard<std::mutex> lock(fake_->mutex);
        fake_->trafficConnects.fetch_add(1);
        if (fake_->trafficConnectFails) {
            return core::MakeError(core::ErrorKind::ConnectionFailed,
                                   "simulated traffic connection failure");
        }
        fake_->trafficHandler = std::move(handler);
        (void)onError;
        return {};
    }

    core::Result<void> ConnectConnections(ConnectionHandler handler, int intervalMs,
                                          ErrorHandler onError) override {
        std::lock_guard<std::mutex> lock(fake_->mutex);
        fake_->connectionConnects.fetch_add(1);
        if (fake_->connectionConnectFails) {
            return core::MakeError(core::ErrorKind::ConnectionFailed,
                                   "simulated connections failure");
        }
        fake_->connectionHandler = std::move(handler);
        fake_->connectionError = std::move(onError);
        (void)intervalMs;
        return {};
    }

    core::Result<void> ConnectLogs(LogHandler handler, ErrorHandler) override {
        std::lock_guard<std::mutex> lock(fake_->mutex);
        fake_->logHandler = std::move(handler);
        return {};
    }

    void Disconnect() override {
        std::lock_guard<std::mutex> lock(fake_->mutex);
        fake_->eventDisconnects.fetch_add(1);
        fake_->trafficHandler = nullptr;
        fake_->connectionHandler = nullptr;
        fake_->logHandler = nullptr;
        fake_->connectionError = nullptr;
    }

private:
    std::shared_ptr<FakeKernel> fake_;
};

class FakeFactory final : public runtime::KernelDriverFactory {
public:
    explicit FakeFactory(std::shared_ptr<FakeKernel> fake) : fake_(std::move(fake)) {}

    std::unique_ptr<runtime::KernelProcessDriver> CreateProcess() override {
        return std::make_unique<FakeProcessDriver>(fake_);
    }
    std::unique_ptr<runtime::KernelApiDriver> CreateApi(
        const mihomo::MihomoApiConfig& config) override {
        (void)config;
        return std::make_unique<FakeApiDriver>(fake_);
    }
    std::unique_ptr<runtime::KernelEventDriver> CreateEvents(
        const mihomo::MihomoEventConfig& config) override {
        (void)config;
        return std::make_unique<FakeEventDriver>(fake_);
    }

private:
    std::shared_ptr<FakeKernel> fake_;
};

struct RecordingObserver final : runtime::KernelSessionObserver {
    mutable std::mutex mutex;
    std::vector<runtime::KernelSnapshot> snapshots;
    std::vector<std::pair<double, double>> traffic;
    std::vector<int> connections;
    std::vector<mihomo::ConnectionsSnapshot> connectionFrames;
    std::vector<std::vector<mihomo::MihomoLogEntry>> logBatches;
    std::vector<ConnectionCloseResult> closeResults;

    void OnKernelConnections(std::uint64_t generation, const mihomo::ConnectionsSnapshot& snapshot) override {
        KernelSessionObserver::OnKernelConnections(generation, snapshot);
        std::lock_guard<std::mutex> lock(mutex);
        connectionFrames.push_back(snapshot);
    }

    void OnKernelLogs(std::uint64_t, const std::vector<mihomo::MihomoLogEntry>& batch) override {
        std::lock_guard<std::mutex> lock(mutex);
        logBatches.push_back(batch);
    }

    void OnKernelConnectionClose(const ConnectionCloseResult& result) override {
        std::lock_guard<std::mutex> lock(mutex);
        closeResults.push_back(result);
    }

    void OnKernelSnapshot(const runtime::KernelSnapshot& snapshot) override {
        std::lock_guard<std::mutex> lock(mutex);
        snapshots.push_back(snapshot);
    }

    void OnKernelTraffic(double upload, double download) override {
        std::lock_guard<std::mutex> lock(mutex);
        traffic.emplace_back(upload, download);
    }

    void OnKernelConnections(int count) override {
        std::lock_guard<std::mutex> lock(mutex);
        connections.push_back(count);
    }

    runtime::KernelSnapshot Last() const {
        std::lock_guard<std::mutex> lock(mutex);
        return snapshots.empty() ? runtime::KernelSnapshot{} : snapshots.back();
    }

    bool HasPhase(runtime::KernelPhase phase) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& snapshot : snapshots) {
            if (snapshot.phase == phase) {
                return true;
            }
        }
        return false;
    }

    std::size_t TrafficCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return traffic.size();
    }

    bool HasTraffic(double upload) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& entry : traffic) {
            if (entry.first == upload) {
                return true;
            }
        }
        return false;
    }
};

struct Harness {
    std::shared_ptr<FakeKernel> fake = std::make_shared<FakeKernel>();
    TempDir dir;
    RecordingObserver observer;
    std::unique_ptr<runtime::KernelSession> session;

    explicit Harness(KernelSession::Dispatcher dispatcher = [](std::function<void()> task) { task(); }) {
        runtime::KernelOptions options;
        options.executablePath = MakeFakeExecutable();
        options.workingDirectory = ToUtf8(dir.path);
        options.configPath = ToUtf8(dir.path / "clashyuka-runtime.yaml");
        options.secret = "test-secret";
        options.controllerPort = 19090;
        options.mixedPort = 17897;
        options.readinessAttempts = 5;
        options.readinessIntervalMs = 5;
        options.readinessTimeoutMs = 20;

        session = std::make_unique<runtime::KernelSession>(
            std::make_unique<FakeFactory>(fake), std::move(dispatcher));
        session->SetOptions(options);
        session->SetObserver(&observer);
    }

    ~Harness() {
        if (session) {
            session->Shutdown();
        }
    }

    std::string MakeFakeExecutable() const {
        const fs::path path = dir.path / "mihomo-fake.exe";
        WriteFile(path, "fake executable");
        return ToUtf8(path);
    }

    runtime::KernelOptions Options() const { return session->Options(); }
    void SetOptions(runtime::KernelOptions options) { session->SetOptions(std::move(options)); }

    bool WaitPhase(runtime::KernelPhase phase, int timeoutMs = 3000) {
        return WaitUntil([this, phase] { return observer.Last().phase == phase; }, timeoutMs);
    }
};

void EmitTraffic(const std::shared_ptr<FakeKernel>& fake, double up, double down) {
    const runtime::KernelEventDriver::TrafficHandler handler = fake->CurrentTrafficHandler();
    if (!handler) {
        return;
    }
    mihomo::TrafficStatistics statistics;
    statistics.up = up;
    statistics.down = down;
    handler(statistics);
}

// ---------------------------------------------------------------------------
// KernelSession
// ---------------------------------------------------------------------------

TEST_CASE("rule provider refresh re-reads both collections and preserves independently valid data") {
    using runtime::RuleProviderUpdateStatus;
    using runtime::RulesLoadStatus;
    Harness harness;
    CHECK_FALSE(harness.session->RefreshRules(0));
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const auto generation = harness.observer.Last().generation;
    REQUIRE(harness.session->RefreshRules(generation));
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.providersStatus == RulesLoadStatus::Ready; }));
    CHECK_FALSE(harness.session->RefreshRules(generation, "inline"));
    CHECK_FALSE(harness.session->RefreshRules(generation, "missing"));
    CHECK_FALSE(harness.session->RefreshRules(generation + 1, "remote"));

    harness.fake->blockProviderUpdate = true;
    REQUIRE(harness.session->RefreshRules(generation, "remote"));
    REQUIRE(WaitUntil([&] { return harness.fake->providerUpdates == 1; }));
    CHECK(harness.observer.Last().rules.loading);
    CHECK_FALSE(harness.session->RefreshRules(generation, "remote"));
    harness.fake->blockProviderUpdate = false;
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.updateStatus == RuleProviderUpdateStatus::Updated; }));
    const auto refreshed = harness.observer.Last().rules;
    REQUIRE(refreshed.rules);
    REQUIRE(refreshed.providers);
    CHECK(refreshed.rules->size() == 2);
    CHECK(refreshed.providers->providers[0].ruleCount == 2);
    CHECK(harness.fake->RulesRequests() == std::vector<std::string>{"rules", "providers",
          "update remote", "rules", "providers"});

    harness.fake->failProviderUpdate = true;
    REQUIRE(harness.session->RefreshRules(generation, "remote"));
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.updateStatus == RuleProviderUpdateStatus::UpdateFailed; }));
    CHECK(harness.observer.Last().rules.rules == refreshed.rules);
    CHECK(harness.observer.Last().rules.providers == refreshed.providers);
    CHECK(harness.fake->RulesRequests().size() == 6);

    harness.fake->failProviderUpdate = false;
    harness.fake->failProviderRead = true;
    REQUIRE(harness.session->RefreshRules(generation, "remote"));
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.updateStatus == RuleProviderUpdateStatus::ReadbackFailed; }));
    const auto partial = harness.observer.Last().rules;
    REQUIRE(partial.rules);
    CHECK(partial.rules->size() == 3);
    CHECK(partial.rulesStatus == RulesLoadStatus::Ready);
    CHECK(partial.providers == refreshed.providers);
    CHECK(partial.providersStatus == RulesLoadStatus::Failed);
}

TEST_CASE("rule refresh drops an in-flight update when the kernel generation changes") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const auto generation = harness.observer.Last().generation;
    REQUIRE(harness.session->RefreshRules(generation));
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.providers != nullptr; }));
    harness.fake->blockProviderUpdate = true;
    REQUIRE(harness.session->RefreshRules(generation, "remote"));
    REQUIRE(WaitUntil([&] { return harness.fake->providerUpdates == 1; }));
    harness.session->Restart();
    CHECK_FALSE(harness.observer.Last().rulesAvailable);
    CHECK_FALSE(harness.observer.Last().rules.providers);
    harness.fake->blockProviderUpdate = false;
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const auto restarted = harness.observer.Last();
    CHECK(restarted.generation > generation);
    CHECK(restarted.rules == runtime::RulesSnapshot{});
    CHECK(harness.fake->RulesRequests() == std::vector<std::string>{"rules", "providers", "update remote"});
    CHECK_FALSE(harness.session->RefreshRules(generation));
    CHECK(harness.session->RefreshRules(restarted.generation));
    REQUIRE(WaitUntil([&] { return harness.observer.Last().rules.providers != nullptr; }));
}

TEST_CASE("kernel session starts, reports the version and connects events") {
    Harness harness;
    harness.session->Start();

    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const runtime::KernelSnapshot snapshot = harness.observer.Last();
    CHECK(snapshot.version == "v1.19.0-test");
    CHECK(snapshot.apiAddress == "127.0.0.1:19090");
    CHECK(snapshot.error.kind == core::ErrorKind::None);
    CHECK(harness.fake->processStarts.load() == 1);
    CHECK(harness.fake->trafficConnects.load() == 1);
    CHECK(harness.fake->connectionConnects.load() == 1);

    const mihomo::MihomoLaunchOptions launch = harness.fake->LastLaunch();
    REQUIRE(launch.arguments.size() == 4);
    CHECK(launch.arguments[0] == "-d");
    CHECK(launch.arguments[1] == ToUtf8(harness.dir.path));
    CHECK(launch.arguments[2] == "-f");
    CHECK(fs::exists(FromUtf8(launch.arguments[3])));
}

TEST_CASE("duplicate start requests are ignored") {
    Harness harness;
    runtime::KernelOptions options = harness.Options();
    options.readinessAttempts = 10;
    harness.SetOptions(options);
    harness.fake->ConfigureApiFailures(0, 30);

    harness.session->Start();
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    CHECK(harness.fake->processStarts.load() == 1);
}

TEST_CASE("stop is idempotent and clears live state") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));

    harness.session->Stop();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Stopped));
    harness.session->Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    CHECK(harness.session->Phase() == runtime::KernelPhase::Stopped);
    CHECK(harness.fake->processStops.load() == 1);
    CHECK(harness.observer.Last().version.empty());
}

TEST_CASE("restart creates a new generation and a new process") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const std::uint64_t firstGeneration = harness.observer.Last().generation;

    harness.session->Restart();
    REQUIRE(WaitUntil([&] {
        const runtime::KernelSnapshot snapshot = harness.observer.Last();
        return snapshot.phase == runtime::KernelPhase::Running &&
               snapshot.generation > firstGeneration;
    }));
    CHECK(harness.fake->processStarts.load() == 2);
    CHECK(harness.fake->processStops.load() >= 1);
}

TEST_CASE("a missing executable fails before the process starts") {
    Harness harness;
    runtime::KernelOptions options = harness.Options();
    options.executablePath = ToUtf8(harness.dir.path / "missing.exe");
    harness.SetOptions(options);

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Failed));
    CHECK(harness.observer.Last().error.kind == core::ErrorKind::KernelNotFound);
    CHECK(harness.fake->processStarts.load() == 0);
}

TEST_CASE("an invalid existing config fails without starting the process") {
    Harness harness;
    WriteFile(harness.dir.path / "clashyuka-runtime.yaml", "key: [unclosed");

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Failed));
    CHECK(harness.observer.Last().error.kind == core::ErrorKind::ConfigFailed);
    CHECK(harness.fake->processStarts.load() == 0);
}

TEST_CASE("a process that exits during startup reports a start failure") {
    Harness harness;
    harness.fake->ConfigureExitAfterStart(7);

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Failed));
    const runtime::KernelSnapshot snapshot = harness.observer.Last();
    CHECK(snapshot.error.kind == core::ErrorKind::StartFailed);
    CHECK(snapshot.error.message.find('7') != std::string::npos);
    CHECK(harness.fake->processStops.load() == 1);
}

TEST_CASE("readiness retries and then succeeds") {
    Harness harness;
    harness.fake->ConfigureApiFailures(2, 0);

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    CHECK(harness.fake->apiCalls.load() == 3);
}

TEST_CASE("readiness timeout rolls the kernel back") {
    Harness harness;
    runtime::KernelOptions options = harness.Options();
    options.readinessAttempts = 3;
    options.readinessIntervalMs = 1;
    options.readinessTimeoutMs = 5;
    harness.SetOptions(options);
    harness.fake->ConfigureApiFailures(1000, 0);

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Failed));
    CHECK(harness.observer.Last().error.kind == core::ErrorKind::Timeout);
    CHECK(harness.fake->apiCalls.load() == 3);
    CHECK(harness.fake->processStops.load() == 1);
}

TEST_CASE("stop cancels a pending readiness wait") {
    Harness harness;
    runtime::KernelOptions options = harness.Options();
    options.readinessAttempts = 100;
    options.readinessIntervalMs = 5;
    options.readinessTimeoutMs = 50;
    harness.SetOptions(options);
    harness.fake->ConfigureApiFailures(1000, 20);

    harness.session->Start();
    REQUIRE(WaitUntil([&] { return harness.fake->apiCalls.load() >= 1; }));
    harness.session->Stop();

    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Stopped, 2000));
    CHECK_FALSE(harness.observer.HasPhase(runtime::KernelPhase::Running));
    CHECK(harness.fake->apiCalls.load() < 100);
    CHECK(harness.fake->processStops.load() == 1);
}

TEST_CASE("shutdown cancels readiness and joins the worker") {
    Harness harness;
    runtime::KernelOptions options = harness.Options();
    options.readinessAttempts = 100;
    options.readinessIntervalMs = 5;
    options.readinessTimeoutMs = 50;
    harness.SetOptions(options);
    harness.fake->ConfigureApiFailures(1000, 20);

    harness.session->Start();
    REQUIRE(WaitUntil([&] { return harness.fake->apiCalls.load() >= 1; }));

    const auto start = std::chrono::steady_clock::now();
    harness.session->Shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(harness.session->Phase() == runtime::KernelPhase::Stopped);
    CHECK(harness.fake->processStops.load() == 1);
    CHECK_FALSE(harness.observer.HasPhase(runtime::KernelPhase::Running));

    harness.session->Shutdown();  // idempotent
}

TEST_CASE("old event callbacks cannot pollute a new generation") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    const std::uint64_t firstGeneration = harness.observer.Last().generation;
    const runtime::KernelEventDriver::TrafficHandler oldHandler =
        harness.fake->CurrentTrafficHandler();
    REQUIRE(oldHandler != nullptr);

    harness.session->Restart();
    REQUIRE(WaitUntil([&] {
        const runtime::KernelSnapshot snapshot = harness.observer.Last();
        return snapshot.phase == runtime::KernelPhase::Running &&
               snapshot.generation > firstGeneration;
    }));

    const std::size_t before = harness.observer.TrafficCount();
    oldHandler(mihomo::TrafficStatistics{999.0, 999.0, 0.0, 0.0});
    CHECK(harness.observer.TrafficCount() == before);

    EmitTraffic(harness.fake, 42.0, 24.0);
    REQUIRE(WaitUntil([&] { return harness.observer.HasTraffic(42.0); }));
}

TEST_CASE("a failing event stream does not stop the kernel") {
    Harness harness;
    harness.fake->ConfigureEventFailures(true, true);

    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));
    CHECK_FALSE(harness.observer.Last().eventError.empty());
    CHECK(harness.session->Phase() == runtime::KernelPhase::Running);
}

TEST_CASE("a process exit after startup is reported as a failure") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));

    harness.fake->SimulateExit(3);
    REQUIRE(WaitUntil(
        [&] { return harness.observer.Last().phase == runtime::KernelPhase::Failed; }));
    const runtime::KernelSnapshot snapshot = harness.observer.Last();
    CHECK(snapshot.error.kind == core::ErrorKind::StartFailed);
    CHECK(snapshot.error.message.find('3') != std::string::npos);
    CHECK(harness.fake->processStops.load() >= 1);
}

TEST_CASE("live traffic and connection counts reach the observer") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(runtime::KernelPhase::Running));

    EmitTraffic(harness.fake, 1024.0, 2048.0);
    REQUIRE(WaitUntil([&] { return harness.observer.HasTraffic(1024.0); }));

    const runtime::KernelEventDriver::ConnectionHandler handler =
        harness.fake->CurrentConnectionHandler();
    REQUIRE(handler != nullptr);
    mihomo::ConnectionsSnapshot snapshot;
    snapshot.connections.resize(3);
    handler(snapshot);
    REQUIRE(WaitUntil([&] {
        std::lock_guard<std::mutex> lock(harness.observer.mutex);
        return !harness.observer.connections.empty() &&
               harness.observer.connections.back() == 3;
    }));
}

TEST_CASE("connection availability recovers after a valid full snapshot") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    CHECK(harness.observer.Last().connectionStream.availability == StreamAvailability::Waiting);
    const auto emit = harness.fake->CurrentConnectionHandler();
    emit({});
    REQUIRE(WaitUntil([&] {
        std::lock_guard<std::mutex> lock(harness.observer.mutex);
        return harness.observer.connectionFrames.size() == 1;
    }));
    CHECK(harness.observer.Last().connectionStream.availability == StreamAvailability::Available);
    CHECK(harness.observer.connectionFrames.back().connections.empty());
    harness.fake->connectionError(core::MakeError(core::ErrorKind::ConnectionFailed, "private error"));
    CHECK(harness.observer.Last().connectionStream.availability == StreamAvailability::Disconnected);
    CHECK_FALSE(harness.observer.Last().connectionStream.error.empty());
    CHECK(harness.observer.Last().logStream.availability == StreamAvailability::Waiting);
    CHECK(harness.session->Phase() == KernelPhase::Running);
    CHECK_FALSE(harness.session->CloseConnection(harness.observer.Last().generation, "active"));

    mihomo::ConnectionsSnapshot snapshot;
    snapshot.downloadTotal = 123;
    snapshot.connections.push_back({"active"});
    snapshot.connections.back().metadata.host = "example.test";
    emit(snapshot);
    const auto recovered = harness.observer.Last();
    CHECK(recovered.connectionStream.availability == StreamAvailability::Available);
    CHECK(recovered.connectionStream.error.empty());
    CHECK(recovered.eventError.empty());
    CHECK(harness.observer.connectionFrames.back().downloadTotal == 123);
    CHECK(harness.observer.connectionFrames.back().connections.front().metadata.host == "example.test");
}

TEST_CASE("log dispatch batches the newest entries and drops queued batches on restart") {
    std::mutex dispatchMutex;
    std::vector<std::function<void()>> pending;
    const auto drain = [&] {
        std::vector<std::function<void()>> jobs;
        {
            std::lock_guard<std::mutex> lock(dispatchMutex);
            jobs.swap(pending);
        }
        for (auto& job : jobs) {
            job();
        }
    };
    Harness harness([&](std::function<void()> job) {
        std::lock_guard<std::mutex> lock(dispatchMutex);
        pending.push_back(std::move(job));
    });
    auto options = harness.Options();
    options.logRedactions = {"https://example.test/sub?token=hidden"};
    harness.SetOptions(std::move(options));
    harness.session->Start();
    const auto waitRunning = [&](std::uint64_t after) {
        return WaitUntil([&] {
            drain();
            const auto current = harness.observer.Last();
            return current.phase == KernelPhase::Running && current.generation > after;
        });
    };
    REQUIRE(waitRunning(0));
    const auto firstGeneration = harness.observer.Last().generation;
    const auto oldLog = harness.fake->logHandler;
    REQUIRE(oldLog != nullptr);
    for (int i = 0; i < 505; ++i) {
        oldLog({"debug", std::to_string(i) + " test-secret https://example.test/sub?token=hidden "
                         + std::string(5000, 'x')});
    }
    REQUIRE(WaitUntil([&] {
        drain();
        return harness.observer.logBatches.size() == 1;
    }));
    const auto& batch = harness.observer.logBatches.front();
    REQUIRE(batch.size() == 500);
    CHECK(batch.front().payload.starts_with("5 [redacted] [redacted] "));
    CHECK(batch.back().payload.starts_with("504 [redacted] [redacted] "));
    CHECK(batch.back().payload.size() == 4096);
    CHECK(harness.observer.Last().logStream.availability == StreamAvailability::Available);

    auto updated = harness.Options();
    const std::string newUrl = "https://example.test/" + std::string(4300, 'a') + "?token=edited";
    oldLog({"info", newUrl});  // Already buffered and truncated before the URL was known.
    updated.logRedactions.push_back(newUrl);
    harness.SetOptions(std::move(updated));
    oldLog({"info", "test-secret " + newUrl + " https://example.test/sub?token=hidden"});
    REQUIRE(WaitUntil([&] {
        drain();
        return harness.observer.logBatches.size() == 2;
    }));
    REQUIRE(harness.observer.logBatches.back().size() == 1);
    CHECK(harness.observer.logBatches.back().front().payload == "[redacted] [redacted] [redacted]");

    oldLog({"info", "queued before restart"});
    harness.session->Restart();
    REQUIRE(waitRunning(firstGeneration));
    oldLog({"info", "late from previous generation"});
    drain();
    CHECK(harness.observer.logBatches.size() == 2);
    harness.fake->logHandler({"info", "new session"});
    REQUIRE(WaitUntil([&] {
        drain();
        return harness.observer.logBatches.size() == 3;
    }));
    CHECK(harness.observer.logBatches.back().front().payload == "new session");
}

TEST_CASE("single connection close is serialized and its result is guarded") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    const auto generation = harness.observer.Last().generation;
    const auto emit = harness.fake->CurrentConnectionHandler();
    mihomo::ConnectionsSnapshot snapshot;
    snapshot.connections.push_back({"active"});
    snapshot.connections.push_back({"second"});
    emit(snapshot);

    CHECK_FALSE(harness.session->CloseConnection(generation + 1, "active"));
    CHECK_FALSE(harness.session->CloseConnection(generation, "unknown"));
    harness.fake->blockClose = true;
    const auto request = harness.session->CloseConnection(generation, "active");
    REQUIRE(request);
    REQUIRE(WaitUntil([&] { return harness.fake->closeCalls.load() == 1; }));
    CHECK_FALSE(harness.session->CloseConnection(generation, "active"));
    CHECK_FALSE(harness.session->CloseConnection(generation, "second"));
    harness.fake->blockClose = false;
    const auto waitResults = [&](std::size_t count) {
        return WaitUntil([&] {
            std::lock_guard<std::mutex> lock(harness.observer.mutex);
            return harness.observer.closeResults.size() == count;
        });
    };
    REQUIRE(waitResults(1));
    CHECK(harness.observer.closeResults.front().requestId == *request);
    CHECK(harness.observer.closeResults.front().generation == generation);
    CHECK(harness.observer.closeResults.front().connectionId == "active");
    CHECK_FALSE(harness.observer.closeResults.front().error);
    {
        std::lock_guard<std::mutex> lock(harness.fake->mutex);
        CHECK(harness.fake->lastClosedId == "active");
    }
    REQUIRE(harness.observer.connectionFrames.size() == 1);
    CHECK(harness.observer.connectionFrames.back().connections.size() == 2);

    harness.fake->failClose = true;
    REQUIRE(harness.session->CloseConnection(generation, "second"));
    REQUIRE(waitResults(2));
    CHECK(harness.observer.closeResults.back().error.httpStatus == 404);
    CHECK(harness.observer.closeResults.back().error.message.find("private") == std::string::npos);

    harness.fake->blockClose = true;
    REQUIRE(harness.session->CloseConnection(generation, "active"));
    REQUIRE(WaitUntil([&] { return harness.fake->closeCalls.load() == 3; }));
    harness.session->Restart();
    harness.fake->blockClose = false;
    REQUIRE(WaitUntil([&] {
        const auto current = harness.observer.Last();
        return current.phase == KernelPhase::Running && current.generation > generation;
    }));
    CHECK(harness.observer.closeResults.size() == 2);
    emit(snapshot);
    CHECK_FALSE(harness.session->CloseConnection(harness.observer.Last().generation, "active"));
}

TEST_CASE("imported copies isolate ingress and preserve group behavior") {
    TempDir dir;
    const std::string original = R"(allow-lan: true
bind-address: '*'
mixed-port: 1
port: 2
socks-port: 3
redir-port: 4
tproxy-port: 5
listeners: [{name: bad, type: socks, port: 6}]
tun: {enable: true}
external-controller: '0.0.0.0:1111'
external-controller-tls: '0.0.0.0:2222'
external-controller-unix: alternate.sock
external-controller-pipe: alternate-pipe
external-controller-cors: {allow-origins: ['*']}
secret: imported-secret
dns: {enable: true, listen: '0.0.0.0:53', nameserver: [1.1.1.1]}
proxy-providers:
  remote: {type: http, url: 'https://example.test/provider', path: configs/source.yaml}
proxy-groups:
  - {name: Pick, type: url-test, proxies: [DIRECT], use: [remote], interval: 99}
rules: ['MATCH,Pick']
)";
    const auto home = ToUtf8(dir.path / fs::path(u8"配置"));
    const auto source = dir.path / "source.yaml";
    WriteFile(source, original);
    RuntimeConfigDefinition definition;
    definition.secret = "app-secret";
    auto first = MakeProfileRuntimeCopy(home, ReadFile(source), definition);
    auto second = MakeProfileRuntimeCopy(home, ReadFile(source), definition);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto path = FromUtf8(first.value()->configPath);
    CHECK(first.value()->configPath != second.value()->configPath);
    CHECK(path.parent_path() == FromUtf8(first.value()->workingDirectory));
    const auto contents = ReadFile(path);
    const auto root = YAML::Load(contents);
    for (const auto* key : {"port", "socks-port", "redir-port", "tproxy-port", "listeners", "tun",
                             "external-controller-tls", "external-controller-unix",
                             "external-controller-pipe", "external-controller-cors"}) {
        CHECK_FALSE(root[key]);
    }
    CHECK_FALSE(root["allow-lan"].as<bool>());
    CHECK(root["bind-address"].Scalar() == "127.0.0.1");
    CHECK(root["external-controller"].Scalar() == "127.0.0.1:9090");
    CHECK(root["mixed-port"].as<int>() == 7897);
    CHECK(root["secret"].Scalar() == "app-secret");
    CHECK_FALSE(root["dns"]["listen"]);
    CHECK(root["dns"]["enable"].as<bool>());
    CHECK_FALSE(root["proxy-providers"]["remote"]["path"]);
    CHECK(root["proxy-groups"][0]["type"].Scalar() == "url-test");
    CHECK(root["proxy-groups"][0]["use"][0].Scalar() == "remote");
    CHECK(root["rules"][0].Scalar() == "MATCH,Pick");
    CHECK(ReadFile(source) == original);
    first.value().reset();
    CHECK_FALSE(fs::exists(path));
}

TEST_CASE("runtime isolation rejects dependencies without disclosing paths") {
    TempDir dir;
    const auto home = ToUtf8(dir.path);
    for (const auto* yaml : {
             "proxy-providers: {test: {type: file, path: secret-file.yaml}}",
             "geox-url: {geoip: secret-file.yaml}",
             "proxies: [{name: remote, certificate: secret-file.yaml}]",
             "proxies: [{name: remote, type: hysteria, ca: secret-file.yaml}]",
             "proxy-providers: {inline: {type: inline, payload: [{certificate: secret-file.yaml}]}}",
             "external-ui: secret-file.yaml",
             "proxies: []\n---\nsecret: secret-file.yaml"}) {
        const auto copy = MakeProfileRuntimeCopy(home, yaml, {});
        REQUIRE_FALSE(copy.ok());
        CHECK(copy.error().message.find("secret-file") == std::string::npos);
    }
    RuntimeConfigDefinition definition;
    definition.host = "192.0.2.8";
    CHECK_FALSE(MakeProfileRuntimeCopy(home, "{}", definition));
    CHECK_FALSE(fs::exists(dir.path / "session-copies"));
}

std::optional<ProfileSwitchResult> WaitSwitch(Harness& harness, std::uint64_t requestId) {
    std::optional<ProfileSwitchResult> found;
    WaitUntil([&] {
        const auto snapshot = harness.observer.Last();
        if (snapshot.profileSwitchResult && snapshot.profileSwitchResult->requestId == requestId) {
            found = snapshot.profileSwitchResult;
        }
        return found.has_value();
    });
    return found;
}

TEST_CASE("first imported startup preflights its own copy before publishing readiness") {
    Harness harness;
    auto options = harness.Options();
    options.profile = {"local-one", "rules: ['MATCH,DIRECT']\nsecret: never-log-this"};
    harness.SetOptions(options);
    harness.fake->ConfigureApiFailures(1, 30);
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::WaitingForApi));
    CHECK(harness.observer.Last().runningProfileId.empty());
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    CHECK(harness.observer.Last().runningProfileId == "local-one");
    CHECK(harness.fake->preflightStarts == 1);
    CHECK(harness.fake->preflightStops == 1);
    CHECK(harness.fake->processStarts == 1);
    CHECK_FALSE(harness.fake->capturedImportedOutput.load());
    CHECK_FALSE(fs::exists(FromUtf8(options.configPath)));
}

TEST_CASE("first imported startup distinguishes preflight rejection from runtime copy failure") {
    Harness harness;
    auto options = harness.Options();
    options.profile = {"local-one", "rules: ['MATCH,DIRECT']\nsecret: profile-marker"};
    harness.SetOptions(options);

    SUBCASE("Mihomo rejects the generated copy") {
        harness.fake->preflightCode = 7;
        harness.session->Start();
        REQUIRE(harness.WaitPhase(KernelPhase::Failed));
        const auto error = harness.observer.Last().error;
        CHECK(error.kind == core::ErrorKind::ProfilePreflightRejected);
        CHECK(error.message == "Mihomo rejected the profile configuration");
        CHECK(harness.fake->preflightStarts == 1);
        CHECK(harness.fake->preflightStops == 1);
        CHECK_FALSE(harness.fake->capturedImportedOutput.load());
    }
    SUBCASE("the runtime copy directory cannot be created") {
        WriteFile(harness.dir.path / "session-copies", "copy-directory-marker");
        harness.session->Start();
        REQUIRE(harness.WaitPhase(KernelPhase::Failed));
        const auto error = harness.observer.Last().error;
        CHECK(error.kind == core::ErrorKind::ConfigFailed);
        CHECK(error.message == "could not create the runtime copy directory");
        CHECK(harness.fake->preflightStarts == 0);
    }
    CHECK(harness.fake->processStarts == 0);
    CHECK(harness.fake->apiCalls == 0);
    CHECK(harness.observer.Last().runningProfileId.empty());
}

TEST_CASE("unavailable selected profile cannot fall back to a legacy config") {
    Harness harness;
    auto options = harness.Options();
    options.profile = {"missing-profile", std::nullopt};
    harness.SetOptions(options);
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Failed));
    CHECK(harness.observer.Last().error.kind == core::ErrorKind::ConfigFailed);
    CHECK(harness.fake->preflightStarts == 0);
    CHECK(harness.fake->processStarts == 0);
    CHECK_FALSE(fs::exists(FromUtf8(options.configPath)));
}

TEST_CASE("failed or timed-out preflight keeps the exact old session running") {
    Harness harness;
    auto options = harness.Options();
    options.preflightTimeoutMs = 35;
    harness.SetOptions(options);
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    const auto old = harness.observer.Last();
    for (const int code : {7, -1}) {
        harness.fake->preflightCode = code;
        const auto request = harness.session->SwitchProfile({"new", "{}"});
        REQUIRE(request);
        const auto result = WaitSwitch(harness, *request);
        REQUIRE(result);
        CHECK(result->oldProfileId == "builtin");
        CHECK(result->newProfileId == "new");
        CHECK(result->outcome == ProfileSwitchOutcome::Rejected);
        CHECK(result->error.kind == (code < 0 ? core::ErrorKind::Timeout
                                             : core::ErrorKind::ProfilePreflightRejected));
        CHECK(harness.observer.Last().phase == KernelPhase::Running);
        CHECK(harness.observer.Last().generation == old.generation);
        CHECK(harness.observer.Last().runningProfileId == "builtin");
        CHECK(harness.fake->processStops == 0);
    }
    CHECK(harness.fake->preflightStops == 2);
}

TEST_CASE("switch success is tokenized and stale old process callbacks cannot undo it") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    const auto old = harness.observer.Last();
    std::function<void(int)> oldExit;
    {
        std::lock_guard lock(harness.fake->mutex);
        oldExit = harness.fake->exitHandler;
    }
    harness.fake->ConfigureApiFailures(1, 30);
    const auto request = harness.session->SwitchProfile({"new", "{}"});
    REQUIRE(request);
    REQUIRE(harness.WaitPhase(KernelPhase::WaitingForApi));
    CHECK(harness.observer.Last().profileSwitchInProgress);
    CHECK(harness.observer.Last().runningProfileId.empty());
    CHECK_FALSE(harness.session->SwitchProfile({"duplicate", "{}"}));
    REQUIRE(oldExit);
    oldExit(17);
    const auto result = WaitSwitch(harness, *request);
    REQUIRE(result);
    CHECK(result->outcome == ProfileSwitchOutcome::Succeeded);
    CHECK(result->oldProfileId == "builtin");
    CHECK(result->newProfileId == "new");
    CHECK(harness.observer.Last().generation > old.generation);
    CHECK(harness.observer.Last().runningProfileId == "new");
    CHECK(harness.observer.Last().phase == KernelPhase::Running);
    CHECK_FALSE(harness.observer.Last().profileSwitchInProgress);
}

TEST_CASE("candidate readiness failure rolls back exact bytes or reports rollback failure") {
    Harness harness;
    auto options = harness.Options();
    options.readinessAttempts = 1;
    harness.SetOptions(options);
    const std::string old = "# user-managed\nexternal-controller: 127.0.0.1:19080\nmixed-port: 17443\n";
    const auto source = FromUtf8(options.configPath);
    WriteFile(source, old);
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    const auto oldPath = harness.observer.Last().configPath;
    WriteFile(source, "# edited after startup\nexternal-controller: 127.0.0.1:19070\n");
    bool rollbackFails = false;
    SUBCASE("restore succeeds") { rollbackFails = false; }
    SUBCASE("restore fails") { rollbackFails = true; }
    harness.fake->ConfigureApiFailures(rollbackFails ? 2 : 1, 0);
    const auto request = harness.session->SwitchProfile({"new", "rules: ['MATCH,DIRECT']"});
    REQUIRE(request);
    const auto result = WaitSwitch(harness, *request);
    REQUIRE(result);
    CHECK(result->outcome == (rollbackFails ? ProfileSwitchOutcome::RollbackFailed
                                            : ProfileSwitchOutcome::RolledBack));
    CHECK(result->oldProfileId == "builtin");
    CHECK(result->newProfileId == "new");
    CHECK(harness.observer.Last().phase == (rollbackFails ? KernelPhase::Failed : KernelPhase::Running));
    CHECK(harness.observer.Last().runningProfileId == (rollbackFails ? "" : "builtin"));
    CHECK(harness.fake->LastLaunch().arguments.back() == oldPath);
    CHECK(ReadFile(source).find("edited after startup") != std::string::npos);
    {
        std::lock_guard lock(harness.fake->mutex);
        REQUIRE(harness.fake->liveConfigs.size() == 3);
        CHECK(harness.fake->liveConfigs[2] == harness.fake->liveConfigs[0]);
        CHECK(harness.fake->liveConfigs[2] == old);
    }
}

TEST_CASE("Stop cancels a pending preflight and does not launch a candidate") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    harness.fake->preflightCode = -1;
    const auto request = harness.session->SwitchProfile({"new", "{}"});
    REQUIRE(request);
    REQUIRE(WaitUntil([&] { return harness.fake->preflightStarts == 1; }));
    harness.session->Stop();
    REQUIRE(harness.WaitPhase(KernelPhase::Stopped));
    const auto snapshot = harness.observer.Last();
    REQUIRE(snapshot.profileSwitchResult);
    CHECK(snapshot.profileSwitchResult->requestId == *request);
    CHECK(snapshot.profileSwitchResult->outcome == ProfileSwitchOutcome::Cancelled);
    CHECK(snapshot.runningProfileId.empty());
    CHECK(harness.fake->processStarts == 1);
    CHECK(harness.fake->preflightStops == 1);
}

TEST_CASE("shutdown discards a pending switch without publishing late results") {
    Harness harness;
    harness.session->Start();
    REQUIRE(harness.WaitPhase(KernelPhase::Running));
    harness.fake->preflightCode = -1;
    REQUIRE(harness.session->SwitchProfile({"new", "{}"}));
    REQUIRE(WaitUntil([&] { return harness.fake->preflightStarts == 1; }));
    harness.session->Shutdown();
    CHECK(harness.session->Snapshot().phase == KernelPhase::Stopped);
    CHECK_FALSE(harness.session->Snapshot().profileSwitchInProgress);
    CHECK_FALSE(harness.observer.Last().profileSwitchResult);
    CHECK(harness.fake->processStarts == 1);
    CHECK(harness.fake->preflightStops == 1);
}

}  // namespace
