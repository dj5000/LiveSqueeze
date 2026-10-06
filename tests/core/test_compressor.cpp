#include <cmath>
#include <vector>

#include "doctest.h"
#include "lsq/compressor.hpp"
#include "lsq/gain_computer.hpp"
#include "lsq/presets.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

constexpr double kSr = 48000.0;

// Measured gain of a 1 kHz tone at `levelDb` once the compressor has settled.
double steadyGainDb(double levelDb, const Params& p) {
    Compressor c;
    c.prepare(kSr);
    const auto frames = static_cast<std::size_t>(5.0 * kSr);
    const std::vector<float> in = sine(frames, 1000.0, levelDb, kSr);
    std::vector<float> out = in;
    c.process(out.data(), frames, p);
    const auto from = static_cast<std::size_t>(4.0 * kSr);
    return rmsDb(out, from, frames) - rmsDb(in, from, frames);
}

} // namespace

TEST_CASE("steady-state gain matches the static curve") {
    const Params p; // Movie / Night
    for (double level : {-80.0, -62.0, -55.0, -50.0, -45.0, -40.0, -34.0, -30.0, -26.0, -24.0,
                         -20.0, -12.0, -6.0, 0.0}) {
        const double expectDb =
            static_cast<double>(curve::staticGainDb(static_cast<float>(level), p));
        INFO("input level " << level << " dBFS");
        CHECK_NEAR(steadyGainDb(level, p), expectDb, 0.15);
    }
}

TEST_CASE("whispers get louder and explosions get quieter") {
    const Params p;
    CHECK(steadyGainDb(-45.0, p) > 4.5);
    CHECK(steadyGainDb(-8.0, p) < -8.0);
}

TEST_CASE("attack and release follow their time constants") {
    Params p;
    Compressor c;
    c.prepare(kSr);

    // Settle on a -40 dB tone (gain target +3 dB), one frame at a time so the gain can be sampled.
    std::vector<float> tone = sine(static_cast<std::size_t>(9.0 * kSr), 1000.0, -40.0, kSr);
    std::size_t pos = 0;
    auto step = [&] {
        c.process(tone.data() + 2 * pos, 1, p);
        ++pos;
    };
    for (std::size_t i = 0; i < static_cast<std::size_t>(3.0 * kSr); ++i) {
        step();
    }
    const double g0 = static_cast<double>(c.currentGainDb());
    CHECK_NEAR(g0, 3.0, 0.1);

    // Falling gain (attack): drop the target by 12 dB with the makeup control.
    p.makeupDb = -12.0f;
    const double fallTarget = g0 - 12.0;
    const double fall63 = g0 + (fallTarget - g0) * (1.0 - std::exp(-1.0));
    std::size_t n = 0;
    while (static_cast<double>(c.currentGainDb()) > fall63 && n < 48000) {
        step();
        ++n;
    }
    CHECK_NEAR(static_cast<double>(n) / kSr * 1000.0, static_cast<double>(p.attackMs), 0.5);

    for (std::size_t i = 0; i < static_cast<std::size_t>(0.5 * kSr); ++i) {
        step();
    }
    CHECK_NEAR(c.currentGainDb(), fallTarget, 0.1);

    // Rising gain (release): restore the makeup.
    p.makeupDb = 0.0f;
    const double g1 = static_cast<double>(c.currentGainDb());
    const double rise63 = g1 + (g0 - g1) * (1.0 - std::exp(-1.0));
    n = 0;
    while (static_cast<double>(c.currentGainDb()) < rise63 && n < 100000) {
        step();
        ++n;
    }
    CHECK_NEAR(static_cast<double>(n) / kSr * 1000.0, static_cast<double>(p.releaseMs), 5.0);
}

TEST_CASE("one gain is applied to both channels") {
    Params p;
    const std::size_t frames = 48000;
    std::vector<float> in(2 * frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSr;
        in[2 * i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * t));      // loud left
        in[2 * i + 1] = static_cast<float>(0.03 * std::sin(2.0 * kPi * 3000.0 * t)); // quiet right
    }
    std::vector<float> out = in;
    Compressor c;
    c.prepare(kSr);
    c.process(out.data(), frames, p);

    int compared = 0;
    for (std::size_t i = 24000; i < frames; ++i) {
        if (std::fabs(in[2 * i]) > 0.1f && std::fabs(in[2 * i + 1]) > 0.01f) {
            const double gl = static_cast<double>(out[2 * i]) / static_cast<double>(in[2 * i]);
            const double gr =
                static_cast<double>(out[2 * i + 1]) / static_cast<double>(in[2 * i + 1]);
            CHECK_NEAR(gl, gr, 1e-5);
            ++compared;
        }
    }
    CHECK(compared > 100);
}

TEST_CASE("max-link reduces more than power-average when one channel is loud") {
    auto minGain = [](bool linkMax) {
        Params p;
        p.linkMax = linkMax;
        const std::size_t frames = 96000;
        std::vector<float> x(2 * frames, 0.0f);
        for (std::size_t i = 0; i < frames; ++i) {
            x[2 * i] = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kSr));
        }
        Compressor c;
        c.prepare(kSr);
        return c.process(x.data(), frames, p).minGainDb;
    };
    CHECK(minGain(true) < minGain(false) - 1.0f);
}

TEST_CASE("disabling the compressor returns the gain to unity") {
    Params p;
    const std::size_t frames = static_cast<std::size_t>(6.0 * kSr);
    std::vector<float> x = sine(frames, 1000.0, -10.0, kSr);
    Compressor c;
    c.prepare(kSr);
    c.process(x.data(), static_cast<std::size_t>(kSr), p); // compress for a second
    CHECK(c.currentGainDb() < -3.0f);

    p.compEnabled = false;
    std::vector<float> y = sine(frames, 1000.0, -10.0, kSr);
    std::vector<float> out = y;
    c.process(out.data(), frames, p);
    CHECK_NEAR(c.currentGainDb(), 0.0, 0.01);
    CHECK_NEAR(rmsDb(out, frames - 4800, frames), rmsDb(y, frames - 4800, frames), 0.01);
}

TEST_CASE("silence is left alone: no boost below the noise floor") {
    Params p;
    Compressor c;
    c.prepare(kSr);
    std::vector<float> x(2 * 48000, 0.0f);
    const CompStats s = c.process(x.data(), 48000, p);
    CHECK(s.minGainDb == 0.0f);
    CHECK(s.maxGainDb == 0.0f);
    CHECK(peakLin(x) == 0.0);

    // Very quiet hiss at -75 dB sits below the noise floor and must not be boosted.
    std::vector<float> hiss = noise(96000, static_cast<float>(std::pow(10.0, -75.0 / 20.0)));
    const CompStats h = c.process(hiss.data(), 96000, p);
    CHECK(h.maxGainDb < 0.5f);
}

TEST_CASE("the sidechain high-pass keeps rumble from triggering compression") {
    Params withHpf;
    withHpf.thresholdDb = -30.0f;
    Params noHpf = withHpf;
    noHpf.sidechainHpf = false;
    auto gain = [&](const Params& p) {
        Compressor c;
        c.prepare(kSr);
        std::vector<float> x = sine(static_cast<std::size_t>(3.0 * kSr), 20.0, -6.0, kSr);
        return c.process(x.data(), x.size() / 2, p).minGainDb;
    };
    CHECK(gain(withHpf) > gain(noHpf) + 3.0f); // 20 Hz is ~24 dB down through the 80 Hz filter
}
