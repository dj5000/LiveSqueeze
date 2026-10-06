#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "doctest.h"
#include "fake_backend.hpp"
#include "lsq/presets.hpp"
#include "lsq/supervisor.hpp"

using namespace lsq;
using namespace std::chrono_literals;

namespace {

// Polls `cond` for up to `timeout`.
template <class F> bool waitFor(F cond, std::chrono::milliseconds timeout = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return cond();
}

SupervisorConfig fastConfig() {
    SupervisorConfig c;
    c.backoffMinMs = 20;
    c.backoffMaxMs = 120;
    c.debounceMs = 20;
    c.watchdogMs = 400;
    c.devicePollMs = 0;
    return c;
}

// Hands out FakeBackends. `last` is the most recent one that was actually opened (the supervisor
// also creates throw-away instances to list devices), and `opened` counts successful opens.
struct Factory {
    FakeConfig cfg;
    std::atomic<FakeBackend*> last{nullptr};
    std::atomic<int> opened{0};
    std::atomic<int> attempts{0};
    std::atomic<int> failOpensRemaining{0};

    BackendFactory make() {
        return [this]() -> std::unique_ptr<IAudioBackend> {
            FakeConfig c = cfg;
            c.onOpen = [this](FakeBackend* b) {
                last = b;
                ++opened;
            };
            c.onDestroy = [this](FakeBackend* b) {
                FakeBackend* expected = b;
                last.compare_exchange_strong(expected, nullptr);
            };
            if (failOpensRemaining.load() > 0) {
                --failOpensRemaining;
                c.failOpen = true;
                ++attempts;
            }
            return std::make_unique<FakeBackend>(c);
        };
    }
};

} // namespace

TEST_CASE("supervisor starts audio and reports statistics") {
    Factory f;
    ParamStore store;
    store.publish(presetParams(Preset::MovieNight));
    Supervisor sup(f.make(), &store);
    CHECK(sup.state() == SupervisorState::Idle);

    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    CHECK(sup.statusText().find("Fake") != std::string::npos);
    REQUIRE(waitFor([&] { return sup.stats().playbackCallbacks > 20; }));
    CHECK(sup.stats().captureCallbacks > 20);
    CHECK(sup.negotiated().captureRate == 48000.0);

    // Meter frames flow to the caller.
    MeterFrame m;
    CHECK(waitFor([&] { return sup.popMeter(m); }));

    sup.stop();
    CHECK(sup.state() == SupervisorState::Idle);
    CHECK(sup.stats().playbackCallbacks == 0); // engine is gone
}

TEST_CASE("supervisor reconnects after a device is removed") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    REQUIRE(waitFor([&] { return sup.stats().playbackCallbacks > 10; }));
    CHECK(f.opened.load() == 1);

    REQUIRE(f.last.load() != nullptr);
    f.last.load()->injectEvent(DeviceEventKind::Removed);
    REQUIRE(waitFor([&] { return f.opened.load() >= 2; }));
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    REQUIRE(waitFor([&] { return sup.stats().playbackCallbacks > 10; }));
    CHECK(sup.restarts() >= 1);
}

TEST_CASE("supervisor retries a failing open with backoff and recovers") {
    Factory f;
    f.failOpensRemaining = 3;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    sup.start(fastConfig());

    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Recovering; }));
    CHECK(sup.lastError().find("open failed") != std::string::npos);
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    CHECK(f.attempts.load() == 3); // three failed opens, then success
    CHECK(f.opened.load() == 1);
}

TEST_CASE("the watchdog restarts audio that stops responding") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    REQUIRE(waitFor([&] { return sup.stats().playbackCallbacks > 10; }));

    // The fake device stops delivering callbacks (a hung driver): stop its thread without telling
    // the supervisor.
    REQUIRE(f.last.load() != nullptr);
    f.last.load()->stop();
    REQUIRE(waitFor([&] { return f.opened.load() >= 2; }, 6000ms));
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }, 6000ms));
}

TEST_CASE("changing the settings restarts with the new configuration") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    const double before = sup.stats().targetFillMs;

    SupervisorConfig c = fastConfig();
    c.latency = LatencyMode::Safe;
    sup.start(c);
    REQUIRE(waitFor([&] { return f.opened.load() >= 2; }));
    REQUIRE(waitFor([&] {
        return sup.state() == SupervisorState::Running && sup.stats().targetFillMs > before + 3.0;
    }));
}

TEST_CASE("an input that monitors the output is refused: it would feed back") {
    Factory f;
    f.cfg.captureMonitorsPlayback = true; // "Monitor of Fake speakers"
    ParamStore store;
    Supervisor sup(f.make(), &store);

    SupervisorConfig c = fastConfig();
    c.request.captureId = "fake-capture";
    c.request.playbackId = "fake-playback";
    sup.start(c);
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Failed; }));
    CHECK(sup.lastError().find("same device") != std::string::npos);
    CHECK(f.opened.load() == 0); // nothing was ever opened
}

TEST_CASE("the output is never a virtual cable, since that would feed back into the input") {
    Factory f;
    f.cfg.playbackLooksVirtual = true;
    ParamStore store;
    Supervisor sup(f.make(), &store);

    // Explicitly chosen:
    SupervisorConfig c = fastConfig();
    c.request.playbackId = "fake-playback";
    sup.start(c);
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Failed; }));
    CHECK(sup.lastError().find("virtual cable") != std::string::npos);
    sup.stop();

    // Defaulted: there is no acceptable output at all.
    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Recovering; }));
    CHECK(sup.lastError().find("No output device") != std::string::npos);
    CHECK(f.opened.load() == 0);
}

TEST_CASE("an unknown device is reported, not crashed on") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    SupervisorConfig c = fastConfig();
    c.request.captureId = "no-such-device";
    sup.start(c);
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Recovering; }));
    CHECK(sup.lastError().find("not available") != std::string::npos);
}

TEST_CASE("stop is prompt and repeatable, and the supervisor can start again") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    for (int i = 0; i < 3; ++i) {
        sup.start(fastConfig());
        REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
        const auto t0 = std::chrono::steady_clock::now();
        sup.stop();
        CHECK(std::chrono::steady_clock::now() - t0 < 1000ms);
        sup.stop(); // harmless
        CHECK(sup.state() == SupervisorState::Idle);
    }
}

TEST_CASE("state changes are reported to the callback") {
    Factory f;
    ParamStore store;
    Supervisor sup(f.make(), &store);
    std::mutex m;
    std::vector<SupervisorState> seen;
    sup.setStateCallback([&](SupervisorState s, const std::string&) {
        std::lock_guard<std::mutex> lock(m);
        seen.push_back(s);
    });
    sup.start(fastConfig());
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    std::lock_guard<std::mutex> lock(m);
    REQUIRE(seen.size() >= 2);
    CHECK(seen.front() == SupervisorState::Starting);
    CHECK(seen.back() == SupervisorState::Running);
}

TEST_CASE("the callback trace is written when audio stops") {
#if defined(_WIN32)
    const std::string file =
        std::string(std::getenv("TEMP") != nullptr ? std::getenv("TEMP") : ".") +
        "\\lsq-trace-test.csv";
#else
    char path[] = "/tmp/lsq-trace-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    close(fd);
    const std::string file = path;
#endif
    {
        Factory f;
        ParamStore store;
        Supervisor sup(f.make(), &store);
        SupervisorConfig c = fastConfig();
        c.tracePath = file;
        sup.start(c);
        REQUIRE(waitFor([&] { return sup.stats().playbackCallbacks > 30; }));
        sup.stop();
    }
    std::FILE* in = std::fopen(file.c_str(), "r");
    REQUIRE(in != nullptr);
    char line[256];
    REQUIRE(std::fgets(line, sizeof line, in) != nullptr);
    CHECK(std::string(line) == "time_s,kind,frames,fill,trim_ppm,peak\n");
    int capture = 0;
    int playback = 0;
    double lastTime = -1.0;
    bool ordered = true;
    while (std::fgets(line, sizeof line, in) != nullptr) {
        double t = 0.0;
        char kind[16] = {};
        if (std::sscanf(line, "%lf,%15[a-z]", &t, kind) == 2) {
            (std::string(kind) == "capture" ? capture : playback) += 1;
            ordered = ordered && t >= lastTime - 5e-3;
            lastTime = t;
        }
    }
    std::fclose(in);
    std::remove(file.c_str());
    CHECK(capture > 10);
    CHECK(playback > 10);
    CHECK(ordered);
}
