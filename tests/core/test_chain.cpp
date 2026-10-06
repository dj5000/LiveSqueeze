#include <cmath>
#include <limits>
#include <vector>

#include "doctest.h"
#include "lsq/db.hpp"
#include "lsq/downmix.hpp"
#include "lsq/dynamics_chain.hpp"
#include "lsq/presets.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

// A soundtrack-like test signal: steady quiet "dialogue" (a 220 Hz tone at -40 dBFS) with a loud
// noise "explosion" for 0.2 s at the start of every 3 s.
std::vector<float> soundtrack(std::size_t frames, double sr) {
    std::vector<float> x(2 * frames);
    Rng rng;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double dialogue = 0.01 * std::sin(2.0 * kPi * 220.0 * t);
        const double boom = (std::fmod(t, 3.0) < 0.2) ? 0.9 * static_cast<double>(rng.next()) : 0.0;
        const auto v = static_cast<float>(dialogue + boom);
        x[2 * i] = v;
        x[2 * i + 1] = v;
    }
    return x;
}

std::vector<float> runChain(const std::vector<float>& in, double sr, const Params& p,
                            std::size_t block) {
    DynamicsChain chain;
    chain.prepare(sr);
    std::vector<float> out = in;
    const std::size_t frames = in.size() / 2;
    for (std::size_t f = 0; f < frames; f += block) {
        chain.process(out.data() + 2 * f, std::min(block, frames - f), p);
    }
    return out;
}

double maxStep(const std::vector<float>& x, std::size_t fromFrame = 1) {
    double m = 0.0;
    for (std::size_t f = fromFrame; f < x.size() / 2; ++f) {
        m = std::max(m, static_cast<double>(std::fabs(x[2 * f] - x[2 * f - 2])));
    }
    return m;
}

} // namespace

TEST_CASE("output is identical for every block size") {
    const std::vector<float> in = soundtrack(96000, 48000.0);
    const Params p = presetParams(Preset::LateNight);
    const std::vector<float> ref = runChain(in, 48000.0, p, 96000);
    for (std::size_t block : {1u, 7u, 64u, 480u, 4096u}) {
        INFO("block size " << block);
        CHECK(runChain(in, 48000.0, p, block) == ref);
    }
}

TEST_CASE("the movie preset narrows the dynamic range and respects the ceiling") {
    const double sr = 48000.0;
    const std::vector<float> in = soundtrack(static_cast<std::size_t>(6.0 * sr), sr);
    const Params p;
    const std::vector<float> out = runChain(in, sr, p, 480);
    CHECK(allFinite(out));
    CHECK(peakLin(out) <= static_cast<double>(dbToLin(p.ceilingDb)));

    // Compare quiet dialogue (long after the previous explosion, so the gain has recovered) with
    // the loud boom. The output is delayed by the chain latency, which is tiny next to these
    // windows.
    auto level = [&](const std::vector<float>& v, double t0, double t1) {
        return rmsDb(v, static_cast<std::size_t>(t0 * sr), static_cast<std::size_t>(t1 * sr));
    };
    const double dialogueIn = level(in, 2.4, 2.9);
    const double dialogueOut = level(out, 2.4, 2.9);
    const double boomIn = level(in, 3.03, 3.15); // the explosion spans t = 3.0 .. 3.2
    const double boomOut = level(out, 3.03, 3.15);
    INFO("dialogue " << dialogueIn << " -> " << dialogueOut << " dB, boom " << boomIn << " -> "
                     << boomOut);
    CHECK_NEAR(dialogueOut - dialogueIn, 3.0, 0.3); // whisper-level dialogue is lifted
    CHECK(boomOut < boomIn - 6.0);                  // the explosion comes down
    CHECK((boomIn - dialogueIn) - (boomOut - dialogueOut) > 9.0); // much less contrast overall
}

TEST_CASE("bypass is a bit-exact delayed copy once the crossfade has finished") {
    const double sr = 48000.0;
    DynamicsChain chain;
    chain.prepare(sr);
    const int lat = chain.latencyFrames();
    Params p;
    p.bypass = true;

    const std::vector<float> in = noise(20000, 0.8f, 11);
    std::vector<float> out = in;
    chain.process(out.data(), 20000, p);

    const auto settled = static_cast<std::size_t>(lat) + 1000; // crossfade is 20 ms = 960 samples
    bool exact = true;
    for (std::size_t f = settled; f < 20000; ++f) {
        exact = exact && out[2 * f] == in[2 * (f - static_cast<std::size_t>(lat))] &&
                out[2 * f + 1] == in[2 * (f - static_cast<std::size_t>(lat)) + 1];
    }
    CHECK(exact);
}

TEST_CASE("bypass still protects against digital clipping") {
    DynamicsChain chain;
    chain.prepare(48000.0);
    Params p;
    p.bypass = true;
    std::vector<float> x = noise(20000, 5.0f);
    chain.process(x.data(), 20000, p);
    CHECK(peakLin(x) <= 1.0);
}

TEST_CASE("toggling bypass does not click") {
    const double sr = 48000.0;
    DynamicsChain chain;
    chain.prepare(sr);
    Params p;
    std::vector<float> x = sine(static_cast<std::size_t>(2.0 * sr), 1000.0, -10.0, sr);
    const std::size_t half = x.size() / 4;
    chain.process(x.data(), half, p);
    p.bypass = true;
    chain.process(x.data() + 2 * half, half, p);
    const double natural = std::pow(10.0, -10.0 / 20.0) * 2.0 * kPi * 1000.0 / sr; // max slope
    CHECK(maxStep(x, 2000) < natural * 1.3);
}

TEST_CASE("output volume changes are ramped") {
    const double sr = 48000.0;
    DynamicsChain chain;
    chain.prepare(sr);
    Params p = presetParams(Preset::LimiterOnly);
    std::vector<float> x = sine(static_cast<std::size_t>(sr), 1000.0, -6.0, sr);
    const std::size_t half = x.size() / 4;
    chain.process(x.data(), half, p);
    p.outTrimDb = -30.0f;
    chain.process(x.data() + 2 * half, half, p);
    const double natural = std::pow(10.0, -6.0 / 20.0) * 2.0 * kPi * 1000.0 / sr;
    CHECK(maxStep(x, 2000) < natural * 1.1);
    CHECK(peakLin(x, 2 * (half + 4000)) < 0.6 * std::pow(10.0, -30.0 / 20.0) * 1.05);
}

TEST_CASE("non-finite input becomes silence, never NaN or Inf") {
    DynamicsChain chain;
    chain.prepare(48000.0);
    const Params p;
    std::vector<float> x = noise(10000, 0.2f);
    x[100] = std::numeric_limits<float>::quiet_NaN();
    x[2001] = std::numeric_limits<float>::infinity();
    x[5000] = -std::numeric_limits<float>::infinity();
    chain.process(x.data(), 10000, p);
    CHECK(allFinite(x));
    CHECK(peakLin(x) <= 1.0);
}

TEST_CASE("denormal input is harmless") {
    DynamicsChain chain;
    chain.prepare(48000.0);
    const Params p;
    std::vector<float> x(2 * 48000, 1e-40f); // denormal floats
    chain.process(x.data(), 48000, p);
    CHECK(allFinite(x));
    CHECK(peakLin(x) < 1e-6);
}

TEST_CASE("the audio path does not allocate") {
    const double sr = 48000.0;
    DynamicsChain chain;
    chain.prepare(sr);
    Downmixer dm;
    dm.prepare(ChannelMap::standard(Layout::Surround51), sr);
    Params p;

    std::vector<float> in6 = noise(4800, 0.5f);
    in6.resize(4800 * 6, 0.1f);
    std::vector<float> stereo(2 * 4800);

    // Warm up once, then count.
    dm.process(in6.data(), stereo.data(), 4800, p);
    chain.process(stereo.data(), 4800, p);

    const std::size_t before = allocCount();
    for (int i = 0; i < 20; ++i) {
        p.centerGainDb = static_cast<float>(i % 5);
        p.bypass = (i % 7 == 0);
        dm.process(in6.data(), stereo.data(), 4800, p);
        chain.process(stereo.data(), 4800, p);
        MeterFrame m;
        while (chain.popMeter(m)) {
        }
    }
    CHECK(allFinite(stereo));
    CHECK(allocCount() == before);
}

TEST_CASE("meters report levels and gain changes") {
    const double sr = 48000.0;
    DynamicsChain chain;
    chain.prepare(sr);
    const Params p;
    std::vector<float> x = sine(static_cast<std::size_t>(sr), 1000.0, -6.0, sr);
    for (std::size_t f = 0; f < 48000; f += 480) {
        chain.process(x.data() + 2 * f, 480, p);
    }
    int frames = 0;
    MeterFrame last;
    MeterFrame m;
    while (chain.popMeter(m)) {
        ++frames;
        last = m;
        CHECK_NEAR(m.inPeakDb, -6.0, 0.1);
    }
    CHECK(frames >= 45);
    CHECK(frames <= 55);
    CHECK(last.compGainDb < -3.0f); // -6 dB tone is above the threshold: reduction
    CHECK(last.outPeakDb < last.inPeakDb);
    CHECK(last.detectorLevelDb > -9.0f);
    CHECK(last.detectorLevelDb < -4.0f);
}

TEST_CASE("every preset keeps the ceiling at several sample rates") {
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        const std::vector<float> in = soundtrack(static_cast<std::size_t>(3.0 * sr), sr);
        for (std::size_t i = 0; i < kPresetCount; ++i) {
            const Params p = presetParams(static_cast<Preset>(i));
            const std::vector<float> out = runChain(in, sr, p, 512);
            INFO("preset " << presetKey(static_cast<Preset>(i)) << " at " << sr);
            CHECK(allFinite(out));
            CHECK(peakLin(out) <= static_cast<double>(dbToLin(p.ceilingDb)) * (1.0 + 1e-6) *
                                          static_cast<double>(dbToLin(p.outTrimDb)) +
                                      1e-9);
        }
    }
}

TEST_CASE("latency is reported consistently") {
    DynamicsChain chain;
    chain.prepare(48000.0);
    CHECK(chain.latencyFrames() == Limiter::kInterpolatorTaps / 2 + 240 - 1);
    CHECK(chain.sampleRate() == 48000.0);
}
