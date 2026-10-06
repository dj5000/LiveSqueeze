#include <cmath>
#include <cstdio>

#include "doctest.h"
#include "lsq/drift_controller.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsqtest;

namespace {

struct LoopResult {
    double finalErrorMs = 0.0;
    double maxAbsErrorMs = 0.0;
    double settleSeconds = 0.0; // last time the smoothed error was outside +/-0.3 ms
    double finalTrimPpm = 0.0;
    double maxAbsTrimPpm = 0.0;
    double meanTrimLast100s = 0.0;
    double saturatedFraction = 0.0; // share of periods at the trim limit
};

// A closed-loop model of the capture ring: the producer delivers input at (1 + producerPpm) of
// nominal, the consumer drains 'block' output frames per period at (1 + consumerPpm) of nominal
// while the controller trims the consumption rate. No audio, just frame counts.
LoopResult simulate(double producerPpm, double consumerPpm, double seconds, double rate = 48000.0,
                    double block = 480.0, double jitterFrames = 0.0) {
    DriftController drift;
    const double target = 0.012 * rate;
    drift.configure(rate, target);

    double fill = target;
    double t = 0.0;
    const double dt =
        block / (rate * (1.0 + consumerPpm * 1e-6)); // real seconds per playback period
    Rng rng;
    LoopResult r;
    double trim = 0.0;
    double trimSum = 0.0;
    long trimCount = 0;
    long saturated = 0;
    long periods = 0;
    while (t < seconds) {
        // The playback callback measures the fill, then takes `block` output frames, which consume
        // block * (1 + trim) input frames. Meanwhile the producer delivers for dt seconds.
        const double measured = fill + jitterFrames * static_cast<double>(rng.next());
        trim = drift.update(measured, block / rate);
        fill -= block * (1.0 + trim * 1e-6);
        fill += rate * (1.0 + producerPpm * 1e-6) * dt;
        t += dt;

        const double errMs = (drift.smoothedFill() - target) / rate * 1000.0;
        r.maxAbsErrorMs = std::max(r.maxAbsErrorMs, std::fabs((fill - target) / rate * 1000.0));
        r.maxAbsTrimPpm = std::max(r.maxAbsTrimPpm, std::fabs(trim));
        ++periods;
        saturated += std::fabs(trim) >= DriftController::kMaxTrimPpm - 0.1 ? 1 : 0;
        if (t > seconds - 100.0) {
            trimSum += trim;
            ++trimCount;
        }
        if (std::fabs(errMs) > 0.3) {
            r.settleSeconds = t;
        }
    }
    r.finalErrorMs = (fill - target) / rate * 1000.0;
    r.finalTrimPpm = trim;
    r.meanTrimLast100s = trimCount > 0 ? trimSum / static_cast<double>(trimCount) : 0.0;
    r.saturatedFraction =
        periods > 0 ? static_cast<double>(saturated) / static_cast<double>(periods) : 0.0;
    return r;
}

} // namespace

TEST_CASE("drift controller converges for a range of clock offsets") {
    struct Case {
        double producerPpm, consumerPpm;
    };
    for (Case c : {Case{0, 0}, Case{100, 0}, Case{-100, 0}, Case{0, 100}, Case{300, -100},
                   Case{-300, 100}, Case{50, 40}}) {
        const LoopResult r = simulate(c.producerPpm, c.consumerPpm, 300.0);
        const double expectedTrim =
            (1.0 + c.producerPpm * 1e-6) / (1.0 + c.consumerPpm * 1e-6) * 1e6 - 1e6;
        std::printf("    [drift] producer %+5.0f ppm, consumer %+5.0f ppm: settled after %5.1f s, "
                    "max error %.2f ms, trim %+.1f ppm (expected %+.1f)\n",
                    c.producerPpm, c.consumerPpm, r.settleSeconds, r.maxAbsErrorMs, r.finalTrimPpm,
                    expectedTrim);
        INFO("producer " << c.producerPpm << " ppm, consumer " << c.consumerPpm << " ppm");
        CHECK(r.settleSeconds < 60.0);
        CHECK(r.maxAbsErrorMs < 3.0);
        CHECK_NEAR(r.finalErrorMs, 0.0, 0.2);
        CHECK_NEAR(r.finalTrimPpm, expectedTrim, 3.0);
        CHECK(r.maxAbsTrimPpm <= DriftController::kMaxTrimPpm + 1e-9);
    }
}

TEST_CASE("measurement jitter does not destabilise the loop") {
    // The fill measurement is off by up to +/-240 frames (5 ms): half a callback period. The
    // trim is noisy but must stay centred on the true offset and not hit the limit.
    const LoopResult r = simulate(80.0, -20.0, 400.0, 48000.0, 480.0, 240.0);
    std::printf("    [drift] 5 ms measurement noise: mean trim %+.1f ppm (expected +100), "
                "at the limit %.2f%% of the time\n",
                r.meanTrimLast100s, 100.0 * r.saturatedFraction);
    CHECK_NEAR(r.meanTrimLast100s, 100.0, 15.0);
    CHECK(r.saturatedFraction < 0.01);
}

TEST_CASE("the trim is limited to +/-2000 ppm") {
    // A 5000 ppm offset is beyond what the controller may correct; it must saturate, not diverge.
    const LoopResult r = simulate(5000.0, 0.0, 60.0);
    CHECK(r.maxAbsTrimPpm <= DriftController::kMaxTrimPpm + 1e-9);
    CHECK(r.maxAbsTrimPpm >= DriftController::kMaxTrimPpm - 1.0);
}

// A device clock off by 0.1% (1000 ppm) is beyond +/-500 but inside the limit: it must still lock.
TEST_CASE("a large clock error is still followed") {
    const LoopResult r = simulate(1000.0, 0.0, 300.0);
    CHECK_NEAR(r.finalTrimPpm, 1000.0, 5.0);
    CHECK(r.maxAbsErrorMs < 6.0);
}

TEST_CASE("reset clears the controller state") {
    DriftController d;
    d.configure(48000.0, 576.0);
    for (int i = 0; i < 100; ++i) {
        d.update(2000.0, 0.01);
    }
    CHECK(d.trimPpm() > 100.0);
    d.reset();
    CHECK(d.trimPpm() == 0.0);
    CHECK(d.update(576.0, 0.01) == 0.0); // right on target: no trim
}
