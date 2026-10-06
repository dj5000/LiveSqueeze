#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "doctest.h"
#include "fake_backend.hpp"
#include "lsq/db.hpp"
#include "lsq/engine.hpp"
#include "lsq/presets.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

// Watches the engine's output without storing it, so long simulations stay cheap.
struct OutputProbe {
    std::uint64_t frames = 0;
    std::uint64_t skipFrames = 0; // ignore the start-up transient
    float last = 0.0f;
    double maxStep = 0.0; // largest sample-to-sample change on the left channel
    double sumSq = 0.0;
    std::uint64_t measured = 0;
    std::uint64_t silentFrames = 0; // frames after skipFrames where the output was exactly zero
    std::uint64_t zeroCrossings = 0;
    bool finite = true;
    double peak = 0.0;

    void operator()(const float* stereo, std::uint32_t n) {
        for (std::uint32_t i = 0; i < n; ++i, ++frames) {
            const float v = stereo[2 * i];
            finite = finite && std::isfinite(v) && std::isfinite(stereo[2 * i + 1]);
            if (frames >= skipFrames) {
                maxStep = std::max(maxStep, static_cast<double>(std::fabs(v - last)));
                sumSq += static_cast<double>(v) * static_cast<double>(v);
                ++measured;
                if (v == 0.0f && stereo[2 * i + 1] == 0.0f) {
                    ++silentFrames;
                }
                if ((last < 0.0f) != (v < 0.0f) && last != 0.0f) {
                    ++zeroCrossings;
                }
                peak = std::max(peak, static_cast<double>(std::fabs(v)));
            }
            last = v;
        }
    }
    double rmsDbFs() const {
        return 10.0 *
               std::log10(std::max(
                   sumSq / static_cast<double>(std::max<std::uint64_t>(measured, 1)), 1e-24));
    }
};

// An Engine fed by a FakeBackend in virtual time.
struct Rig {
    ParamStore store;
    Engine engine{&store};
    OutputProbe probe;
    std::unique_ptr<FakeBackend> fake;
    NegotiatedInfo info;

    explicit Rig(FakeConfig cfg, LatencyMode mode = LatencyMode::Balanced,
                 Preset preset = Preset::LimiterOnly) {
        cfg.sink = [this](const float* s, std::uint32_t n) { probe(s, n); };
        fake = std::make_unique<FakeBackend>(std::move(cfg));
        AudioCallbacks cb;
        cb.user = &engine;
        cb.capture = [](void* u, const float* in, std::uint32_t n) {
            static_cast<Engine*>(u)->onCapture(in, n);
        };
        cb.playback = [](void* u, float* out, std::uint32_t n) {
            static_cast<Engine*>(u)->onPlayback(out, n);
        };
        REQUIRE(fake->open(OpenRequest{}, cb, nullptr, nullptr).ok);
        // The engine timestamps capture callbacks; give it the simulation's clock.
        engine.setClock([](void* f) { return static_cast<FakeBackend*>(f)->now(); }, fake.get());
        info = fake->info();
        EngineConfig ec;
        ec.captureRate = info.captureRate;
        ec.playbackRate = info.playbackRate;
        ec.captureMap = info.captureMap;
        ec.captureBlock = info.capturePeriodFrames;
        ec.playbackBlock = info.playbackPeriodFrames;
        ec.latency = mode;
        REQUIRE(engine.configure(ec).ok);
        store.publish(presetParams(preset));
    }
};

// Largest per-sample change of a 1 kHz sine of amplitude 0.1 at 48 kHz.
constexpr double kNaturalStep = 0.1 * 2.0 * kPi * 1000.0 / 48000.0;

} // namespace

TEST_CASE("clean clocks: continuous, glitch-free, one start-up") {
    FakeConfig cfg;
    Rig rig(cfg);
    rig.probe.skipFrames = 48000;
    rig.fake->advance(30.0);

    const EngineStats s = rig.engine.stats();
    CHECK(s.underruns == 0);
    CHECK(s.overruns == 0);
    CHECK(s.overfillSkips == 0);
    CHECK(s.primes == 1);
    CHECK(s.running);
    CHECK_NEAR(s.fillMs, s.targetFillMs, 1.0);
    CHECK(rig.probe.finite);
    CHECK(rig.probe.silentFrames == 0);
    CHECK(rig.probe.maxStep <= kNaturalStep * 1.02);
    CHECK_NEAR(rig.probe.rmsDbFs(), toDb(0.1 / std::sqrt(2.0)), 0.05); // untouched level
}

TEST_CASE("clock drift of +/-100 ppm with 2 ms of jitter over five minutes") {
    struct Case {
        double capPpm, playPpm;
    };
    for (Case c : {Case{100.0, 0.0}, Case{-100.0, 0.0}, Case{50.0, -50.0}}) {
        FakeConfig cfg;
        cfg.captureClockPpm = c.capPpm;
        cfg.playbackClockPpm = c.playPpm;
        cfg.jitterMs = 2.0;
        cfg.seed = 5;
        Rig rig(cfg);
        rig.probe.skipFrames = 48000 * 10;
        rig.fake->advance(300.0);

        const EngineStats s = rig.engine.stats();
        // Capture clock fast relative to playback means the ring wants to grow: positive trim.
        const double expectedTrim = (1.0 + c.capPpm * 1e-6) / (1.0 + c.playPpm * 1e-6) * 1e6 - 1e6;
        std::printf(
            "    [engine] capture %+4.0f ppm, playback %+4.0f ppm: fill %.2f ms (target %.2f), "
            "trim %+.1f ppm (expected %+.1f), underruns %llu, overruns %llu\n",
            c.capPpm, c.playPpm, s.fillMs, s.targetFillMs, s.trimPpm, expectedTrim,
            static_cast<unsigned long long>(s.underruns),
            static_cast<unsigned long long>(s.overruns));
        INFO("capture " << c.capPpm << " ppm, playback " << c.playPpm << " ppm");
        CHECK(s.underruns == 0);
        CHECK(s.overruns == 0);
        CHECK(s.overfillSkips == 0);
        CHECK(s.primes == 1);
        CHECK_NEAR(s.fillMs, s.targetFillMs, 4.0);
        CHECK_NEAR(s.trimPpm, expectedTrim, 40.0);
        CHECK(rig.probe.finite);
        CHECK(rig.probe.silentFrames == 0);
        CHECK(rig.probe.maxStep <= kNaturalStep * 1.05); // no clicks anywhere in five minutes
    }
}

TEST_CASE("different device rates: 44.1 kHz capture to 48 kHz playback") {
    FakeConfig cfg;
    cfg.captureRate = 44100.0;
    cfg.playbackRate = 48000.0;
    cfg.capturePeriod = 441;
    cfg.playbackPeriod = 480;
    cfg.captureClockPpm = 30.0;
    cfg.jitterMs = 1.0;
    Rig rig(cfg);
    rig.probe.skipFrames = 48000 * 2;
    rig.fake->advance(60.0);

    const EngineStats s = rig.engine.stats();
    CHECK(s.underruns == 0);
    CHECK(s.overruns == 0);
    CHECK(rig.probe.maxStep <= kNaturalStep * 1.05);
    // The tone is 1 kHz in the capture device's time; over 58 s it must still be 1 kHz (+30 ppm).
    const double expectedCrossings = 58.0 * 1000.0 * 2.0;
    CHECK_NEAR(static_cast<double>(rig.probe.zeroCrossings), expectedCrossings, 6.0);
    CHECK_NEAR(rig.probe.rmsDbFs(), toDb(0.1 / std::sqrt(2.0)), 0.1);
}

TEST_CASE("a capture stall is covered by a short fade, then audio resumes") {
    FakeConfig cfg;
    cfg.jitterMs = 1.0;
    Rig rig(cfg);
    rig.probe.skipFrames = 48000;
    rig.fake->advance(5.0);
    CHECK(rig.engine.stats().underruns == 0);

    rig.fake->stallCapture(0.100); // the capture device goes quiet for 100 ms
    rig.fake->advance(5.0);

    const EngineStats s = rig.engine.stats();
    CHECK(s.underruns == 1);
    CHECK(s.primes == 2); // started again once the queue refilled
    CHECK(s.running);
    CHECK_NEAR(s.fillMs, s.targetFillMs, 4.0);
    CHECK(rig.probe.finite);
    CHECK(rig.probe.silentFrames > 2000);           // there was a gap...
    CHECK(rig.probe.silentFrames < 8000);           // ...of roughly the stall length, not much more
    CHECK(rig.probe.maxStep <= kNaturalStep * 1.1); // and it was entered and left smoothly
}

TEST_CASE("a playback stall that overfills the queue is trimmed back to target") {
    FakeConfig cfg;
    Rig rig(cfg);
    rig.probe.skipFrames = 48000;
    rig.fake->advance(5.0);

    rig.fake->stallPlayback(1.0); // a second is more than the 500 ms ring can hold
    rig.fake->advance(5.0);

    const EngineStats s = rig.engine.stats();
    CHECK(s.overruns >= 1);      // capture had to drop frames while the queue was full
    CHECK(s.overfillSkips == 1); // and the excess was discarded on resume
    CHECK(s.underruns == 0);
    CHECK_NEAR(s.fillMs, s.targetFillMs, 4.0);
    CHECK(rig.probe.finite);
    CHECK(rig.probe.peak <= 0.1 * 1.001); // the tone, within resampler ripple
}

TEST_CASE("5.1 capture is downmixed and compressed, and respects the ceiling") {
    FakeConfig cfg;
    cfg.captureMap = ChannelMap::standard(Layout::Surround51);
    cfg.captureClockPpm = 60.0;
    cfg.jitterMs = 1.5;
    // A loud 5.1 signal: a 220 Hz tone on every channel at -6 dBFS.
    const double rate = cfg.captureRate;
    cfg.generator = [rate](float* out, std::uint32_t frames, std::uint64_t start, int ch) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            const auto v = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(start + i) / rate));
            for (int c = 0; c < ch; ++c) {
                out[static_cast<std::size_t>(i) * static_cast<std::size_t>(ch) +
                    static_cast<std::size_t>(c)] = v;
            }
        }
    };
    Rig rig(cfg, LatencyMode::Balanced, Preset::MovieNight);
    rig.probe.skipFrames = 48000 * 3;
    rig.fake->advance(30.0);

    const EngineStats s = rig.engine.stats();
    CHECK(s.underruns == 0);
    CHECK(s.overruns == 0);
    CHECK(rig.probe.finite);
    // Unity-ish downmix of a -6 dB tone would be near +2 dBFS; the chain must hold the ceiling.
    CHECK(rig.probe.peak <= static_cast<double>(dbToLin(-1.0f)) + 1e-6);
    CHECK(rig.probe.peak > 0.05); // and it is actually playing something
}

TEST_CASE("latency modes trade delay for margin") {
    double latency[3];
    double target[3];
    int i = 0;
    for (LatencyMode m : {LatencyMode::Low, LatencyMode::Balanced, LatencyMode::Safe}) {
        FakeConfig cfg;
        Rig rig(cfg, m);
        rig.fake->advance(2.0);
        const EngineStats s = rig.engine.stats();
        latency[i] = s.latencyMs;
        target[i] = s.targetFillMs;
        CHECK(s.underruns == 0);
        ++i;
    }
    std::printf("    [engine] added latency: low %.1f ms, balanced %.1f ms, safe %.1f ms\n",
                latency[0], latency[1], latency[2]);
    CHECK(latency[0] < latency[1]);
    CHECK(latency[1] < latency[2]);
    CHECK(latency[1] < 40.0);
    CHECK(target[0] < target[1]);
}

TEST_CASE("configure rejects nonsense and an unconfigured engine is silent") {
    Engine e;
    float out[2 * 64];
    for (float& v : out) {
        v = 1.0f;
    }
    e.onPlayback(out, 64);
    for (float v : out) {
        CHECK(v == 0.0f);
    }
    EngineConfig bad;
    bad.captureRate = 100.0;
    CHECK_FALSE(e.configure(bad).ok);
    EngineConfig bad2;
    bad2.captureMap.n = 0;
    CHECK_FALSE(e.configure(bad2).ok);
    CHECK(e.configure(EngineConfig{}).ok);
}

TEST_CASE("huge playback callbacks are split, not overrun") {
    FakeConfig cfg;
    cfg.playbackPeriod = 20000; // far above maxPlaybackBlock
    cfg.capturePeriod = 480;
    Rig rig(cfg);
    rig.probe.skipFrames = 48000;
    rig.fake->advance(10.0);
    CHECK(rig.probe.finite);
    CHECK(rig.engine.stats().playbackCallbacks > 5);
}

TEST_CASE("larger callbacks than advertised enlarge the queue instead of underrunning forever") {
    // The device says 256-frame callbacks but really delivers 1024: as a PipeWire graph running a
    // bigger quantum than asked for would.
    FakeConfig cfg;
    cfg.capturePeriod = 1024;
    cfg.playbackPeriod = 1024;
    cfg.jitterMs = 1.0;
    Rig rig(cfg);
    // Tell the engine the wrong sizes (Rig used the true ones); rebuild with bad hints.
    EngineConfig ec = rig.engine.config();
    ec.captureBlock = 256;
    ec.playbackBlock = 256;
    REQUIRE(rig.engine.configure(ec).ok);
    rig.store.publish(presetParams(Preset::LimiterOnly));
    rig.probe.skipFrames = 48000 * 4;
    const double target0 = rig.engine.stats().targetFillMs;

    rig.fake->advance(20.0);
    const EngineStats s = rig.engine.stats();
    std::printf("    [engine] hint 256, actual 1024: target %.1f -> %.1f ms, adaptations %llu, "
                "underruns %llu\n",
                target0, s.targetFillMs, static_cast<unsigned long long>(s.adaptations),
                static_cast<unsigned long long>(s.underruns));
    CHECK(s.adaptations >= 1);
    CHECK(s.targetFillMs > target0 + 10.0);
    CHECK(s.underruns <= 3);            // while it found out, never afterwards
    CHECK(rig.probe.silentFrames == 0); // nothing after the first four seconds is lost
    CHECK(rig.probe.maxStep <= kNaturalStep * 1.05);
}
