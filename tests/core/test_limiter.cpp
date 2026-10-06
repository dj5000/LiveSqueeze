#include <cmath>
#include <cstdio>
#include <vector>

#include "doctest.h"
#include "lsq/db.hpp"
#include "lsq/limiter.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

struct LimiterRun {
    std::vector<float> out;
    int latency = 0;
    float minGain = 1.0f;
};

LimiterRun runLimiter(const std::vector<float>& in, double sr, const Params& p,
                      std::size_t block = 480) {
    Limiter lim;
    lim.prepare(sr);
    LimiterRun r;
    r.latency = lim.latencyFrames();
    r.out = in;
    const std::size_t frames = in.size() / 2;
    for (std::size_t f = 0; f < frames; f += block) {
        const std::size_t n = std::min(block, frames - f);
        r.minGain = std::min(r.minGain, lim.process(r.out.data() + 2 * f, n, p));
    }
    return r;
}

// True peak (dB) of the first channel, measured with the independent oracle.
double truePeakDb(const std::vector<float>& x) {
    return toDb(oracleTruePeak(x, 2, 0));
}

} // namespace

TEST_CASE("an impulse comes out unchanged, delayed by exactly the reported latency") {
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        std::vector<float> in(2 * 4000, 0.0f);
        in[2 * 100] = 0.5f;
        in[2 * 100 + 1] = -0.25f;
        const Params p;
        const LimiterRun r = runLimiter(in, sr, p);
        INFO("sample rate " << sr);
        const auto at = static_cast<std::size_t>(100 + r.latency);
        CHECK(r.out[2 * at] == 0.5f);
        CHECK(r.out[2 * at + 1] == -0.25f);
        double rest = 0.0;
        for (std::size_t f = 0; f < 4000; ++f) {
            if (f != at) {
                rest += std::fabs(static_cast<double>(r.out[2 * f])) +
                        std::fabs(static_cast<double>(r.out[2 * f + 1]));
            }
        }
        CHECK(rest == 0.0);
        CHECK(r.minGain == 1.0f);
    }
}

TEST_CASE("latency is the interpolator delay plus the look-ahead") {
    Limiter lim;
    lim.prepare(48000.0);
    CHECK(lim.latencyFrames() == Limiter::kInterpolatorTaps / 2 + 240 - 1);
    lim.prepare(96000.0);
    CHECK(lim.latencyFrames() == Limiter::kInterpolatorTaps / 2 + 480 - 1);
}

TEST_CASE("signals below the ceiling pass bit-exactly") {
    const Params p; // ceiling -1 dBTP = 0.891
    // White noise is a poor "quiet" test signal: its reconstructed waveform overshoots the sample
    // peak by several dB, so it is kept well below the ceiling. The sine is below it in both
    // senses.
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (int kind = 0; kind < 2; ++kind) {
            const std::vector<float> in =
                kind == 0 ? noise(20000, 0.15f) : sine(20000, 997.0, -2.0, sr);
            const LimiterRun r = runLimiter(in, sr, p);
            INFO("sample rate " << sr << (kind == 0 ? ", noise" : ", sine"));
            CHECK(r.minGain == 1.0f);
            const auto lat = static_cast<std::size_t>(r.latency);
            bool exact = true;
            for (std::size_t f = lat; f < 20000; ++f) {
                exact = exact && r.out[2 * f] == in[2 * (f - lat)] &&
                        r.out[2 * f + 1] == in[2 * (f - lat) + 1];
            }
            CHECK(exact);
        }
    }
}

TEST_CASE("sample peaks never exceed the ceiling, however hard the input") {
    for (double ceilDb : {-0.1, -1.0, -6.0, -12.0}) {
        Params p;
        p.ceilingDb = static_cast<float>(ceilDb);
        const float ceiling = dbToLin(p.ceilingDb);
        for (float amp : {1.0f, 3.0f, 30.0f}) {
            const LimiterRun r = runLimiter(noise(48000, amp, 7), 48000.0, p);
            INFO("ceiling " << ceilDb << " dB, noise amplitude " << amp);
            CHECK(peakLin(r.out) <= static_cast<double>(ceiling));
            CHECK(allFinite(r.out));
        }
    }
}

TEST_CASE("a sudden loud onset is caught before it arrives") {
    // Silence, then a full-scale-plus sine that starts abruptly.
    std::vector<float> in(2 * 24000, 0.0f);
    const std::vector<float> burst = sine(12000, 997.0, 6.0, 48000.0, 1.0);
    std::copy(burst.begin(), burst.end(), in.begin() + 2 * 12000);
    const Params p;
    const LimiterRun r = runLimiter(in, 48000.0, p);
    CHECK(peakLin(r.out) <= static_cast<double>(dbToLin(p.ceilingDb)));
    // The limiter must have actually worked, not just clipped: the output keeps the sine's shape.
    CHECK(peakLin(r.out) > 0.85);
}

namespace {

struct TpCase {
    const char* name;
    std::vector<float> signal;
    double toleranceDb; // allowed true-peak excess over the ceiling
};

std::vector<TpCase> adversarialSignals() {
    constexpr double sr = 48000.0;
    const std::size_t n = 24000;
    std::vector<TpCase> cases;

    // Samples peak at 0.84 but the continuous waveform reaches 1.19: invisible to a sample meter.
    cases.push_back({"fs/4 sine, 45 deg phase, +1.5 dB", sine(n, 12000.0, 1.5, sr, kPi / 4), 0.2});
    cases.push_back({"1 kHz sine, +3 dB", sine(n, 1000.0, 3.0, sr), 0.1});
    cases.push_back({"15 kHz sine, +2 dB", sine(n, 15000.0, 2.0, sr, 0.3), 0.2});
    cases.push_back({"19 kHz sine, +2 dB", sine(n, 19000.0, 2.0, sr, 0.7), 0.35});

    {
        std::vector<float> sq(2 * n);
        for (std::size_t i = 0; i < n; ++i) {
            const float v = ((i / 24) % 2 == 0) ? 1.0f : -1.0f; // 1 kHz square
            sq[2 * i] = sq[2 * i + 1] = v;
        }
        cases.push_back({"1 kHz square wave, 0 dBFS", sq, 0.1});
    }
    {
        // Pairs of adjacent full-scale samples: the waveform between them reaches 1.27.
        std::vector<float> pairs(2 * n, 0.0f);
        for (std::size_t i = 100; i + 1 < n; i += 64) {
            const float s = ((i / 64) % 2 == 0) ? 1.0f : -1.0f;
            pairs[2 * i] = pairs[2 * i + 1] = s;
            pairs[2 * (i + 1)] = pairs[2 * (i + 1) + 1] = s;
        }
        cases.push_back({"adjacent sample pairs, 0 dBFS", pairs, 0.15});
    }
    {
        std::vector<float> bursts(2 * n, 0.0f);
        Rng rng;
        for (std::size_t i = 0; i < n; ++i) {
            if ((i / 2000) % 2 == 0) {
                bursts[2 * i] = bursts[2 * i + 1] = 2.0f * rng.next();
            }
        }
        // Full-band white noise 12 dB over full scale is the extreme case: its reconstructed peaks
        // depend on many distant samples, so a finite detector misjudges some of them by up to
        // about 0.7 dB. Real programme material is far kinder (next case).
        cases.push_back({"white noise bursts, +6 dB (extreme)", bursts, 1.0});
    }
    {
        std::vector<float> lp(2 * n, 0.0f);
        Rng rng;
        float y = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            y = 0.5f * y + 0.5f * rng.next();
            if ((i / 2000) % 2 == 0) {
                lp[2 * i] = lp[2 * i + 1] = 3.0f * y;
            }
        }
        cases.push_back({"low-passed noise bursts, +9 dB", lp, 0.5});
    }
    {
        std::vector<float> sweep(2 * n);
        double phase = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double f =
                100.0 + (20000.0 - 100.0) * static_cast<double>(i) / static_cast<double>(n);
            phase += 2.0 * kPi * f / sr;
            sweep[2 * i] = sweep[2 * i + 1] = static_cast<float>(1.5 * std::sin(phase));
        }
        cases.push_back({"sine sweep 100 Hz - 20 kHz, +3.5 dB", sweep, 0.35});
    }
    return cases;
}

} // namespace

TEST_CASE("true peak after limiting stays at the ceiling (independent 16x oracle)") {
    const Params p; // ceiling -1 dBTP
    for (TpCase& c : adversarialSignals()) {
        const double before = truePeakDb(c.signal);
        const LimiterRun r = runLimiter(c.signal, 48000.0, p);
        const double after = truePeakDb(r.out);
        std::printf("    [true peak] %-40s before %+6.2f dBTP, after %+6.2f dBTP (ceiling %.1f)\n",
                    c.name, before, after, static_cast<double>(p.ceilingDb));
        INFO(c.name);
        CHECK(before > static_cast<double>(p.ceilingDb) + 0.5); // the signal really needed limiting
        CHECK(after <= static_cast<double>(p.ceilingDb) + c.toleranceDb);
        CHECK(after > static_cast<double>(p.ceilingDb) - 6.0); // and was not crushed
    }
}

TEST_CASE("true-peak guarantee holds at other sample rates") {
    const Params p;
    for (double sr : {44100.0, 96000.0}) {
        const std::vector<float> in =
            sine(static_cast<std::size_t>(sr / 2.0), sr / 4.0, 1.5, sr, kPi / 4);
        const LimiterRun r = runLimiter(in, sr, p);
        INFO("sample rate " << sr);
        CHECK(truePeakDb(r.out) <= static_cast<double>(p.ceilingDb) + 0.2);
    }
}

TEST_CASE("release recovers the gain with the configured time constant") {
    // DC makes the gain directly observable: gain = output / delayed input.
    Params p;
    p.limiterReleaseMs = 100.0f;
    const std::size_t burstEnd = 24000; // 0.5 s of 2.0 followed by 0.3
    const std::size_t total = burstEnd + 96000;
    std::vector<float> in(2 * total);
    for (std::size_t i = 0; i < total; ++i) {
        in[2 * i] = in[2 * i + 1] = i < burstEnd ? 2.0f : 0.3f;
    }
    const LimiterRun r = runLimiter(in, 48000.0, p);
    const auto lat = static_cast<std::size_t>(r.latency);

    auto gainAtInput = [&](std::size_t i) {
        return static_cast<double>(r.out[2 * (i + lat)]) / static_cast<double>(in[2 * i]);
    };
    const double ceiling = static_cast<double>(dbToLin(p.ceilingDb));
    CHECK_NEAR(gainAtInput(burstEnd - 1000), ceiling / 2.0, 1e-4);
    // After one time constant, 1 - 1/e of the gap to unity has been closed.
    const double expected = 1.0 - (1.0 - ceiling / 2.0) * std::exp(-1.0);
    CHECK_NEAR(gainAtInput(burstEnd + 4800), expected, 0.04);
    CHECK_NEAR(gainAtInput(burstEnd + 90000), 1.0, 1e-3);
    // Release never overshoots above unity.
    for (std::size_t i = burstEnd; i + lat < total; i += 97) {
        CHECK(gainAtInput(i) <= 1.0 + 1e-6);
    }
}

TEST_CASE("reset clears the delay and the gain state") {
    Limiter lim;
    lim.prepare(48000.0);
    const Params p;
    std::vector<float> loud = noise(5000, 4.0f);
    lim.process(loud.data(), 5000, p);
    lim.reset();
    std::vector<float> silence(2 * 2000, 0.0f);
    lim.process(silence.data(), 2000, p);
    CHECK(peakLin(silence) == 0.0);
}

TEST_CASE("output does not depend on the block size") {
    const std::vector<float> in = noise(30000, 2.0f, 3);
    const Params p;
    const LimiterRun ref = runLimiter(in, 48000.0, p, 30000);
    for (std::size_t block : {1u, 7u, 64u, 480u, 4096u}) {
        CHECK(runLimiter(in, 48000.0, p, block).out == ref.out);
    }
}
