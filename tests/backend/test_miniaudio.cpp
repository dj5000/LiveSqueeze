// Exercises the miniaudio backend through its software-timed "null" devices, so it works on
// machines (and CI runners) without any audio hardware. Real-device behaviour cannot be tested
// here; see docs/HARDWARE_TEST_CHECKLIST.md.

#include <chrono>
#include <thread>

#include "doctest.h"
#include "lsq/backend_factory.hpp"
#include "lsq/presets.hpp"
#include "lsq/supervisor.hpp"

#if defined(LSQ_HAVE_MINIAUDIO)
using namespace lsq;
using namespace std::chrono_literals;

namespace {
template <class F> bool waitFor(F cond, std::chrono::milliseconds timeout = 8000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return cond();
}
} // namespace

TEST_CASE("null devices are listed, with the capture side flagged as a virtual cable") {
    auto b = createBackend(BackendKind::MiniaudioNull);
    REQUIRE(b != nullptr);
    const auto in = b->enumerate(Dir::Capture);
    const auto out = b->enumerate(Dir::Playback);
    REQUIRE(in.size() >= 1);
    REQUIRE(out.size() >= 1);
    CHECK(in[0].looksVirtual);
    CHECK_FALSE(out[0].looksVirtual);
    CHECK_FALSE(in[0].id.empty());
}

TEST_CASE("open, start, run, stop and close the null devices through an engine") {
    auto b = createBackend(BackendKind::MiniaudioNull);
    REQUIRE(b != nullptr);
    ParamStore store;
    store.publish(presetParams(Preset::MovieNight));
    Engine engine(&store);
    AudioCallbacks cb;
    cb.user = &engine;
    cb.capture = [](void* u, const float* in, std::uint32_t n) {
        static_cast<Engine*>(u)->onCapture(in, n);
    };
    cb.playback = [](void* u, float* out, std::uint32_t n) {
        static_cast<Engine*>(u)->onPlayback(out, n);
    };

    REQUIRE(b->open(OpenRequest{}, cb, nullptr, nullptr).ok);
    const NegotiatedInfo info = b->info();
    CHECK(info.captureRate >= 8000.0);
    CHECK(info.playbackRate >= 8000.0);
    CHECK(info.captureMap.n >= 1);
    CHECK(info.capturePeriodFrames > 0);
    CHECK_FALSE(info.captureName.empty());

    EngineConfig ec;
    ec.captureRate = info.captureRate;
    ec.playbackRate = info.playbackRate;
    ec.captureMap = info.captureMap;
    ec.captureBlock = info.capturePeriodFrames;
    ec.playbackBlock = info.playbackPeriodFrames;
    REQUIRE(engine.configure(ec).ok);

    REQUIRE(b->start().ok);
    CHECK(waitFor([&] { return engine.stats().playbackCallbacks > 20; }));
    b->stop();

    const EngineStats s = engine.stats();
    CHECK(s.captureCallbacks > 10);
    CHECK(s.playbackCallbacks > 10);
    CHECK(s.overruns == 0);

    // After stop() no callback may arrive any more.
    const auto frozen = engine.stats().playbackCallbacks;
    std::this_thread::sleep_for(100ms);
    CHECK(engine.stats().playbackCallbacks == frozen);

    b->close();
    // It can be opened again.
    REQUIRE(b->open(OpenRequest{}, cb, nullptr, nullptr).ok);
    REQUIRE(b->start().ok);
    b->stop();
    b->close();
}

TEST_CASE("bad device ids are rejected cleanly") {
    auto b = createBackend(BackendKind::MiniaudioNull);
    REQUIRE(b != nullptr);
    OpenRequest req;
    req.captureId = "not-a-valid-id";
    const Status st = b->open(req, AudioCallbacks{}, nullptr, nullptr);
    CHECK_FALSE(st.ok);
    CHECK(st.message.find("capture") != std::string::npos);
}

TEST_CASE("start before open fails instead of crashing") {
    auto b = createBackend(BackendKind::MiniaudioNull);
    REQUIRE(b != nullptr);
    CHECK_FALSE(b->start().ok);
    b->stop();
    b->close();
}

TEST_CASE("the real backend reports 'no audio system' or lists devices, never crashes") {
    // On a machine with no sound setup this exercises the failure path.
    auto b = createBackend(BackendKind::Miniaudio);
    REQUIRE(b != nullptr);
    (void)b->enumerate(Dir::Capture);
    (void)b->enumerate(Dir::Playback);
    const Status st = b->open(OpenRequest{}, AudioCallbacks{}, nullptr, nullptr);
    if (!st.ok) {
        CHECK_FALSE(st.message.empty());
    }
    b->close();
}

TEST_CASE("a supervisor runs the null devices end to end and shuts down cleanly") {
    ParamStore store;
    store.publish(presetParams(Preset::MovieNight));
    Supervisor sup([] { return createBackend(BackendKind::MiniaudioNull); }, &store);
    SupervisorConfig cfg;
    cfg.devicePollMs = 0;
    sup.start(cfg);
    REQUIRE(waitFor([&] { return sup.state() == SupervisorState::Running; }));
    CHECK(waitFor([&] { return sup.stats().playbackCallbacks > 30; }));
    CHECK(sup.statusText().find("NULL") != std::string::npos);
    sup.stop();
    CHECK(sup.state() == SupervisorState::Idle);
}
#else
TEST_CASE("miniaudio backend is not part of this build") {
    CHECK_FALSE(lsq::backendAvailable(lsq::BackendKind::Miniaudio));
}
#endif
