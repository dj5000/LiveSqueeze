#include <cmath>
#include <cstdio>
#include <vector>

#include "doctest.h"
#include "lsq/async_resampler.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

// Resamples `in` (stereo interleaved, long enough) into `outFrames` frames in chunks of `chunk`.
// `ratioAt(i)` gives the ratio to use for the chunk starting at output frame i.
template <class RatioFn>
std::vector<float> resample(AsyncResampler& rs, const std::vector<float>& in, std::size_t outFrames,
                            std::size_t chunk, RatioFn ratioAt) {
    std::vector<float> out(2 * outFrames);
    std::size_t inPos = 0;
    for (std::size_t o = 0; o < outFrames; o += chunk) {
        const std::size_t n = std::min(chunk, outFrames - o);
        const double ratio = ratioAt(o);
        const std::size_t need = rs.inputFramesNeeded(n, ratio);
        REQUIRE((inPos + need) * 2 <= in.size());
        const std::size_t got =
            rs.process(in.data() + 2 * inPos, need, out.data() + 2 * o, n, ratio);
        REQUIRE(got == n);
        inPos += need;
    }
    return out;
}

} // namespace

TEST_CASE("1:1 conversion is transparent: flat level, almost no distortion") {
    AsyncResampler rs;
    rs.prepare(1.0, 4096);
    const double sr = 48000.0;
    for (double f : {100.0, 1000.0, 10000.0}) {
        rs.reset();
        const std::vector<float> in = sine(60000, f, -6.0, sr);
        const auto out = resample(rs, in, 48000, 480, [](std::size_t) { return 1.0; });
        const SineFit fit = fitSine(out, 2, 0, 2000, 46000, f / sr);
        INFO("frequency " << f);
        CHECK_NEAR(toDb(fit.amplitude), -6.0, 0.01);
        CHECK(fit.residualDb < -85.0);
    }
}

TEST_CASE("high frequencies stay within half a dB up to 15 kHz") {
    AsyncResampler rs;
    rs.prepare(1.0, 4096);
    const double sr = 48000.0;
    for (double f : {12000.0, 15000.0}) {
        rs.reset();
        const std::vector<float> in = sine(60000, f, -6.0, sr);
        const auto out = resample(rs, in, 48000, 480, [](std::size_t) { return 1.0; });
        const SineFit fit = fitSine(out, 2, 0, 2000, 46000, f / sr);
        INFO("frequency " << f);
        CHECK_NEAR(toDb(fit.amplitude), -6.0, 0.5);
    }
}

TEST_CASE("drift trims of +/-500 ppm do not add distortion") {
    const double sr = 48000.0;
    for (double ppm : {-500.0, -100.0, 100.0, 500.0}) {
        for (double f : {1000.0, 10000.0}) {
            AsyncResampler rs;
            rs.prepare(1.0, 4096);
            const double ratio = 1.0 + ppm * 1e-6;
            const std::vector<float> in = sine(60000, f, -6.0, sr);
            const auto out = resample(rs, in, 48000, 480, [&](std::size_t) { return ratio; });
            const SineFit fit = fitSine(out, 2, 0, 2000, 46000, f / sr * ratio);
            INFO("trim " << ppm << " ppm, " << f << " Hz");
            CHECK_NEAR(toDb(fit.amplitude), -6.0, 0.01);
            CHECK(fit.residualDb < -80.0);
        }
    }
}

TEST_CASE("44.1 kHz to 48 kHz and back") {
    struct Case {
        double inRate, outRate;
    };
    for (Case c : {Case{44100.0, 48000.0}, Case{48000.0, 44100.0}, Case{96000.0, 48000.0},
                   Case{48000.0, 96000.0}, Case{48000.0, 192000.0}}) {
        const double nominal = c.inRate / c.outRate;
        AsyncResampler rs;
        rs.prepare(nominal, 8192);
        const double f = 1000.0;
        const std::size_t outFrames = static_cast<std::size_t>(c.outRate);
        const std::size_t inFrames = static_cast<std::size_t>(c.inRate * 1.2) + 8192;
        const std::vector<float> in = sine(inFrames, f, -6.0, c.inRate);
        const auto out = resample(rs, in, outFrames, 480, [&](std::size_t) { return nominal; });
        const SineFit fit = fitSine(out, 2, 0, 2000, outFrames - 2000, f / c.outRate);
        INFO(c.inRate << " -> " << c.outRate);
        CHECK_NEAR(toDb(fit.amplitude), -6.0, 0.02);
        CHECK(fit.residualDb < -80.0);
    }
}

TEST_CASE("content above the output Nyquist rate is rejected, not aliased") {
    // 48 kHz -> 44.1 kHz: a 23.5 kHz tone is above the new Nyquist rate (22.05 kHz).
    const double nominal = 48000.0 / 44100.0;
    AsyncResampler rs;
    rs.prepare(nominal, 8192);
    const std::vector<float> in = sine(60000, 23500.0, -6.0, 48000.0);
    const auto out = resample(rs, in, 44100, 480, [&](std::size_t) { return nominal; });
    const double residual = rmsDb(out, 4000, 40000);
    const double inLevel = rmsDb(in, 4000, 40000);
    std::printf("    [resampler] 23.5 kHz tone through 48k->44.1k: %.1f dB below the input\n",
                inLevel - residual);
    CHECK(inLevel - residual > 40.0);
}

TEST_CASE("inputFramesNeeded and outputFramesPossible agree with process") {
    AsyncResampler rs;
    rs.prepare(1.0884, 16384);
    const std::vector<float> in = noise(250000, 0.5f);
    Rng rng;
    std::size_t inPos = 0;
    for (int i = 0; i < 400; ++i) {
        const std::size_t n = 1 + static_cast<std::size_t>((rng.next() + 1.0f) * 300.0f);
        const double ratio = 1.0884 * (1.0 + static_cast<double>(rng.next()) * 500e-6);
        const std::size_t need = rs.inputFramesNeeded(n, ratio);
        // With exactly `need` frames the whole block can be produced...
        CHECK(rs.outputFramesPossible(need, ratio) >= n);
        // ...and with one frame fewer it cannot (unless nothing was needed).
        if (need > 0) {
            CHECK(rs.outputFramesPossible(need - 1, ratio) < n);
        }
        std::vector<float> out(2 * n);
        REQUIRE((inPos + need) * 2 <= in.size());
        CHECK(rs.process(in.data() + 2 * inPos, need, out.data(), n, ratio) == n);
        inPos += need;
    }
}

TEST_CASE("starved input produces fewer frames, never garbage or a crash") {
    AsyncResampler rs;
    rs.prepare(1.0, 4096);
    const std::vector<float> in = sine(200, 1000.0, -6.0, 48000.0);
    std::vector<float> out(2 * 1000, 0.0f);
    const std::size_t got = rs.process(in.data(), 100, out.data(), 1000, 1.0);
    CHECK(got == 100 - static_cast<std::size_t>(rs.latencyFrames()));
    CHECK(rs.outputFramesPossible(0, 1.0) == 0);
}

TEST_CASE("output does not depend on how the work is chunked") {
    // Splitting the work renormalises the position accumulator at different moments, so results
    // agree to rounding error rather than bit for bit.
    const double ratio = 0.91875;
    const std::vector<float> in = noise(40000, 0.5f);
    auto run = [&](std::size_t chunk) {
        AsyncResampler rs;
        rs.prepare(ratio, 32768); // room for the 20000-frame single call
        return resample(rs, in, 20000, chunk, [&](std::size_t) { return ratio; });
    };
    const auto ref = run(20000);
    for (std::size_t chunk : {1u, 7u, 100u, 480u}) {
        const auto out = run(chunk);
        double worst = 0.0;
        for (std::size_t i = 0; i < ref.size(); ++i) {
            worst = std::max(worst, static_cast<double>(std::fabs(out[i] - ref[i])));
        }
        INFO("chunk " << chunk);
        CHECK(worst < 1e-5);
    }
}
