// Owner-thread stalls (plan/features/0013 "Tests: Owner-thread stalls", ADR-0097): with 32
// in-flight wait_for_state calls and 10 000 log lines/s from the fake J-Link, no owner-thread task
// may take more than 20 ms. A 5 ms QTimer probe measures the dispatch latency (how late each tick
// runs); the largest one bounds the longest task.
//
// Load-relative (plan 0014 implementation notes, user decision 2026-11-04): on a loaded machine
// the probe is late without any long task (28-67 ms were seen with a 20 ms bound). So the same
// probe first measures a baseline on the same event loop under the same conditions (connected,
// the stream running, no waits), and the bound applies to the extra latency: worst with the 32
// waits < baseline worst + the allowance below. A task that blocks the loop for longer than the
// allowance still fails (checked by injecting a 35 ms sleep: 40 ms vs 2 ms + 20 ms). When the probe
// got fewer than half of its expected ticks during the baseline, the machine is too starved to
// tell a stall from preemption and the test is skipped (user decision 2026-11-04). The same holds
// when the bound is exceeded while the 1-minute load average (before or after the run) is above
// the number of CPUs: skipped with the reason, never while the load is at most the CPU count
// (Windows has no load average: never skipped there).

#include "hwio/device/console_line.h"
#include "session_provider.h"
#include "tools_test_support.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtCore/qnamespace.h>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <stdlib.h> // NOLINT(modernize-deprecated-headers): getloadavg (BSD/glibc, not in std)
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

namespace hwio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;

// The allowance applies to the normal (debug / release) builds: 20 ms, 100 ms on macOS, whose CI
// runners showed 29-62 ms of timer and scheduler jitter in Debug (plan 0023: 50 ms; raised to
// 100 ms by the user on 2026-11-03 after 62 ms on a macOS runner).
// ASan/UBSan slow everything down by about 2-3x (bound 100 ms); under TSan (5-15x, and
// DeviceSession's own event batches dominate) the latency is only recorded, not checked.
#ifdef __has_feature
#if __has_feature(thread_sanitizer)
constexpr bool checkLatency = false;
constexpr auto allowedLatency = 1000ms;
#elif __has_feature(address_sanitizer)
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 100ms;
#elifdef __APPLE__
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 100ms;
#else
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 20ms;
#endif
#else
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 20ms;
#endif

// The 1-minute system load average (Linux, macOS); nullopt where it is not available (Windows:
// never skipped).
std::optional<double> loadAverage() {
#ifdef _WIN32
    return std::nullopt;
#else
    std::array<double, 1> load{};
    return getloadavg(load.data(), 1) == 1 ? std::optional(load.front()) : std::nullopt;
#endif
}

TEST(OwnerThread, ThirtyTwoWaitsOnAFastStreamDoNotStall) {
    const std::optional<double> loadBefore = loadAverage();
    SessionProvider provider;
    const QJsonObject scenario{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{
             {QStringLiteral("serialNumber"), 801000001},
             {QStringLiteral("rtt"),
              QJsonObject{
                  {QStringLiteral("up"),
                   QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Terminal")},
                                          {QStringLiteral("size"), 1024}},
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("Logger")},
                                          {QStringLiteral("size"), 65536}}}},
                  {QStringLiteral("stream"),
                   QJsonArray{
                       QJsonObject{{QStringLiteral("buffer"), 1},
                                   {QStringLiteral("linesPerSecond"), 10000},
                                   {QStringLiteral("text"),
                                    QStringLiteral("[00:00:01.000,000] <inf> perf: line")}}}}}}}}}};
    provider.scenario(QJsonDocument(scenario).toJson());
    Harness harness(provider);
    auto connect = harness.call(QStringLiteral("connect"), args({{"device", "jlink:801000001"}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();

    // The largest lateness of a 5 ms probe while the event loop runs for `duration`, and how many
    // ticks the probe got.
    struct Probe {
        std::int64_t worst = 0;
        int ticks = 0;
    };
    QElapsedTimer clock;
    clock.start();
    const auto measure = [&clock](std::chrono::milliseconds duration) {
        Probe result;
        std::int64_t last = clock.elapsed();
        QTimer probe;
        probe.setTimerType(Qt::PreciseTimer);
        probe.setInterval(5ms);
        QObject::connect(&probe, &QTimer::timeout, [&] {
            const std::int64_t now = clock.elapsed();
            result.worst = std::max(result.worst, now - last - 5);
            last = now;
            ++result.ticks;
        });
        probe.start();
        runFor(duration);
        probe.stop();
        return result;
    };
    // The baseline: the same loop and stream without the waits.
    constexpr auto window = 3s;
    constexpr int expectedTicks = static_cast<int>(window / 5ms);
    const Probe baselineProbe = measure(window);
    const std::int64_t baseline = baselineProbe.worst;
    RecordProperty("baseline_ticks", baselineProbe.ticks);
    if (checkLatency && baselineProbe.ticks * 2 < expectedTicks) {
        // User decision 2026-11-04 (plan 0014): a CPU-starved machine cannot show stalls.
        GTEST_SKIP() << "the machine is too loaded to measure owner-thread stalls: the probe got "
                     << baselineProbe.ticks << " of " << expectedTicks
                     << " ticks during the baseline (worst lateness " << baseline << " ms)";
    }

    std::vector<PendingCall> waits;
    waits.reserve(32);
    for (int i = 0; i < 32; ++i) {
        waits.push_back(
            harness.start(QStringLiteral("wait_for_state"),
                          args({{"pattern", QStringLiteral("never (seen|matched) %1$").arg(i)},
                                {"timeout", 600},
                                {"source", "both"}})));
    }
    ASSERT_EQ(harness.server->activeCalls(), 32U);

    const auto log = provider.logOf("jlink:801000001");
    const std::uint64_t before = log->total(hwio::device::Channel::Log);
    const std::int64_t worst = measure(window).worst;
    const std::uint64_t received = log->total(hwio::device::Channel::Log) - before;

    RecordProperty("baseline_dispatch_latency_ms", static_cast<int>(baseline));
    RecordProperty("worst_dispatch_latency_ms", static_cast<int>(worst));
    RecordProperty("log_lines_received", static_cast<int>(received));
    EXPECT_GT(received, 25000U) << "the stream did not run";
    const bool tooSlow = checkLatency && worst >= baseline + allowedLatency.count();
    for (const PendingCall& wait : waits) {
        EXPECT_FALSE(wait.done());
    }
    harness.server->stop();
    EXPECT_TRUE(runUntil([&] { return harness.server->activeCalls() == 0; }));
    if (!tooSlow) {
        return;
    }
    // User decision 2026-11-04 (plan 0014): on a machine with more runnable work than CPUs the loop
    // loses CPU to other processes and a stall cannot be told from preemption. Never skipped while
    // the load is at most the number of CPUs (or unknown).
    const std::optional<double> loadAfter = loadAverage();
    const auto cpus = static_cast<double>(std::thread::hardware_concurrency());
    const double load = std::max(loadBefore.value_or(0.0), loadAfter.value_or(0.0));
    if (cpus > 0 && load > cpus) {
        GTEST_SKIP() << "the machine is overloaded (load average " << load << " > " << cpus
                     << " CPUs): worst lateness " << worst << " ms, baseline " << baseline << " ms";
    }
    ADD_FAILURE() << "an owner-thread task took " << worst
                  << " ms (baseline without the waits: " << baseline << " ms, "
                  << baselineProbe.ticks << " of " << expectedTicks << " ticks, load average "
                  << load << " on " << cpus << " CPUs)";
}

} // namespace
} // namespace hwio::mcp::tools::test
