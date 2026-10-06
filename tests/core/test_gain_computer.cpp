#include <cmath>

#include "doctest.h"
#include "lsq/gain_computer.hpp"
#include "lsq/presets.hpp"
#include "test_util.hpp"

using namespace lsq;
using namespace lsq::curve;

// Default parameters: Td = -24, Rd = 4, knee = 8, Tu = -34, Ru = 2, Bmax = 9, Nf = -60.

TEST_CASE("downward compression follows the closed-form definition") {
    const Params p;
    CHECK(downwardGainDb(-40.0f, p) == 0.0f);
    CHECK(downwardGainDb(-28.0f, p) == 0.0f); // lower edge of the knee
    // Inside the knee the gain is slope * (over + W/2)^2 / (2W), with slope = 1/R - 1 = -0.75.
    CHECK_NEAR(downwardGainDb(-24.0f, p), -0.75 * 16.0 / 16.0, 1e-5);
    CHECK_NEAR(downwardGainDb(-22.0f, p), -0.75 * 36.0 / 16.0, 1e-5);
    CHECK_NEAR(downwardGainDb(-20.0f, p), -3.0, 1e-5); // upper edge of the knee: slope * over
    CHECK_NEAR(downwardGainDb(-10.0f, p), -0.75 * 14.0, 1e-5); // above the knee: (1/R - 1) * over
    CHECK_NEAR(downwardGainDb(0.0f, p), -0.75 * 24.0, 1e-5);
}

TEST_CASE("hard knee is a corner at the threshold") {
    Params p;
    p.kneeDb = 0.0f;
    CHECK(downwardGainDb(-24.0f, p) == 0.0f);
    CHECK(downwardGainDb(-30.0f, p) == 0.0f);
    CHECK_NEAR(downwardGainDb(-14.0f, p), -7.5, 1e-5);
}

TEST_CASE("upward boost rises below the threshold, is capped, and fades out near the noise floor") {
    const Params p;
    CHECK(upwardGainDb(-20.0f, p) == 0.0f);
    CHECK(upwardGainDb(-30.0f, p) == 0.0f);         // upper edge of the knee
    CHECK_NEAR(upwardGainDb(-34.0f, p), 0.5, 1e-5); // middle of the knee
    CHECK_NEAR(upwardGainDb(-40.0f, p), 3.0, 1e-5); // (1 - 1/Ru) * (Tu - L) = 0.5 * 6
    CHECK_NEAR(upwardGainDb(-45.0f, p), 5.5, 1e-5);
    CHECK_NEAR(upwardGainDb(-50.0f, p), 8.0, 1e-5);       // end of the taper, below the cap
    CHECK_NEAR(upwardGainDb(-55.0f, p), 9.0 * 0.5, 1e-5); // capped at 9 dB, half way through taper
    CHECK(upwardGainDb(-60.0f, p) == 0.0f);               // no boost at the noise floor
    CHECK(upwardGainDb(-90.0f, p) == 0.0f);
}

TEST_CASE("null band between the thresholds has exactly zero gain") {
    const Params p;
    for (float level = -29.9f; level <= -28.1f; level += 0.1f) {
        CHECK(staticGainDb(level, p) == 0.0f);
    }
}

TEST_CASE("makeup is added, and everything is off when the compressor is disabled") {
    Params p;
    p.makeupDb = 3.0f;
    CHECK_NEAR(staticGainDb(-29.0f, p), 3.0, 1e-6);
    p.compEnabled = false;
    CHECK(staticGainDb(-29.0f, p) == 0.0f);
    CHECK(staticGainDb(-10.0f, p) == 0.0f);
    CHECK(staticGainDb(-50.0f, p) == 0.0f);
}

TEST_CASE("upward boost can be switched off") {
    Params p;
    p.upwardEnabled = false;
    CHECK(staticGainDb(-50.0f, p) == 0.0f);
    CHECK_NEAR(staticGainDb(-10.0f, p), -10.5, 1e-5);
}

TEST_CASE("slope is continuous at the knee edges") {
    const Params p;
    const float h = 0.01f;
    auto slope = [&](float level) {
        return (downwardGainDb(level + h, p) - downwardGainDb(level - h, p)) / (2.0f * h);
    };
    // Td = -24, W = 8: knee spans -28 .. -20.
    CHECK_NEAR(slope(-28.0f - 0.02f), 0.0, 0.01);
    CHECK_NEAR(slope(-28.0f + 0.02f), 0.0, 0.01);
    CHECK_NEAR(slope(-20.0f - 0.02f), -0.75, 0.01);
    CHECK_NEAR(slope(-20.0f + 0.02f), -0.75, 0.01);

    auto upSlope = [&](float level) {
        return (upwardGainDb(level + h, p) - upwardGainDb(level - h, p)) / (2.0f * h);
    };
    // Tu = -34: knee spans -38 .. -30.
    CHECK_NEAR(upSlope(-30.0f + 0.02f), 0.0, 0.01);
    CHECK_NEAR(upSlope(-30.0f - 0.02f), 0.0, 0.01);
    CHECK_NEAR(upSlope(-38.0f + 0.02f), -0.5, 0.01);
    CHECK_NEAR(upSlope(-38.0f - 0.02f), -0.5, 0.01);
}

TEST_CASE("output level never decreases when the input level increases") {
    // A compressor must not invert the ordering of loudness.
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const Params p = presetParams(static_cast<Preset>(i));
        float prevOut = -1000.0f;
        for (float level = -100.0f; level <= 0.0f; level += 0.05f) {
            const float g = staticGainDb(level, p);
            REQUIRE(std::isfinite(g));
            const float out = level + g;
            INFO("preset " << presetKey(static_cast<Preset>(i)) << " level " << level);
            CHECK(out >= prevOut - 1e-4f);
            prevOut = out;
        }
    }
}

TEST_CASE("typical movie levels: whispers up, dialogue untouched, explosions down") {
    const Params p;                                   // Movie / Night
    CHECK(staticGainDb(-45.0f, p) > 4.0f);            // whisper
    CHECK(std::fabs(staticGainDb(-30.0f, p)) < 0.1f); // dialogue
    CHECK(staticGainDb(-12.0f, p) < -8.0f);           // explosion
}
