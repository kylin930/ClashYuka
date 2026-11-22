#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "clashyuka/mihomo/process.h"

using clashyuka::mihomo::MihomoLaunchOptions;
using clashyuka::mihomo::MihomoProcess;
using clashyuka::mihomo::MihomoProcessHandlers;

namespace {

bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 20) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

MihomoLaunchOptions ProbeOptions(const std::string& sleepMs) {
    MihomoLaunchOptions options;
    options.executablePath = CLASHYUKA_PROCESS_PROBE_PATH;
    options.arguments = {"--sleep", sleepMs};
    return options;
}

}  // namespace

TEST_CASE("process starts, captures output and reports the exit code") {
    MihomoProcess process;
    std::mutex mutex;
    std::string stdoutText;
    std::string stderrText;
    std::atomic<int> exitCode{-1};

    MihomoProcessHandlers handlers;
    handlers.onStdoutLine = [&](std::string_view line) {
        std::lock_guard<std::mutex> lock(mutex);
        stdoutText += std::string(line) + "\n";
    };
    handlers.onStderrLine = [&](std::string_view line) {
        std::lock_guard<std::mutex> lock(mutex);
        stderrText += std::string(line) + "\n";
    };
    handlers.onExited = [&](int code) { exitCode.store(code); };

    MihomoLaunchOptions options;
    options.executablePath = CLASHYUKA_PROCESS_PROBE_PATH;
    options.arguments = {"--sleep", "300", "--exit", "7"};

    REQUIRE(process.Start(options, handlers));
    const auto pid = process.GetPid();
    REQUIRE(pid.has_value());

    CHECK(WaitUntil([&] { return exitCode.load() == 7; }, 5000));
    CHECK(WaitUntil([&] { return !process.IsRunning(); }, 2000));

    {
        std::lock_guard<std::mutex> lock(mutex);
        CHECK(stdoutText.find("ready") != std::string::npos);
        CHECK(stderrText.find("probe-started") != std::string::npos);
    }

    const auto reportedCode = process.GetExitCode();
    REQUIRE(reportedCode.has_value());
    CHECK(reportedCode.value() == 7);
}

TEST_CASE("process refuses duplicate starts and stops idempotently") {
    MihomoProcess process;
    auto options = ProbeOptions("5000");
    REQUIRE(process.Start(options));

    CHECK(process.IsRunning());
    CHECK_FALSE(process.Start(options));

    const auto pid = process.GetPid();
    REQUIRE(pid.has_value());

    process.Stop();
    CHECK_FALSE(process.IsRunning());
    CHECK_FALSE(process.GetPid().has_value());

    process.Stop();
    CHECK_FALSE(process.IsRunning());
}

TEST_CASE("process restart replaces the running instance") {
    MihomoProcess process;
    auto options = ProbeOptions("5000");
    REQUIRE(process.Start(options));
    REQUIRE(process.IsRunning());

    auto restarted = ProbeOptions("200");
    CHECK(process.Restart(restarted));
    CHECK(process.IsRunning());
    CHECK(WaitUntil([&] { return !process.IsRunning(); }, 5000));
}

TEST_CASE("process rejects an empty executable path") {
    MihomoProcess process;
    MihomoLaunchOptions options;
    CHECK_FALSE(process.Start(options));
    CHECK_FALSE(process.IsRunning());
    CHECK_FALSE(process.GetPid().has_value());
}
