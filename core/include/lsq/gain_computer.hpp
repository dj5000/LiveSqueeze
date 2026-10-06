#pragma once

// The static compression curve: detector level (dB) in, gain (dB) out. Pure functions of the
// parameters, with no state, so they are easy to test against the closed-form definition.

#include <algorithm>

#include "lsq/params.hpp"

namespace lsq::curve {

// Width of the fade-in of the upward boost above the noise floor.
inline constexpr float kNoiseTaperDb = 10.0f;

// Gain <= 0 dB from soft-knee downward compression above the threshold.
inline float downwardGainDb(float levelDb, const Params& p) noexcept {
    const float over = levelDb - p.thresholdDb;
    const float slope = 1.0f / std::max(p.ratio, 1.0f) - 1.0f; // <= 0
    const float w = p.kneeDb;
    if (w < 0.01f) {
        return over > 0.0f ? slope * over : 0.0f;
    }
    if (2.0f * over < -w) {
        return 0.0f;
    }
    if (2.0f * over <= w) {
        const float t = over + 0.5f * w;
        return slope * t * t / (2.0f * w);
    }
    return slope * over;
}

// Gain >= 0 dB from the upward boost below the upward threshold, capped at maxBoostDb and faded
// out towards the noise floor.
inline float upwardGainDb(float levelDb, const Params& p) noexcept {
    if (!p.upwardEnabled) {
        return 0.0f;
    }
    const float below = p.upThresholdDb - levelDb;
    const float slope = 1.0f - 1.0f / std::max(p.upRatio, 1.0f); // >= 0
    const float w = p.kneeDb;
    float boost;
    if (w < 0.01f) {
        boost = below > 0.0f ? slope * below : 0.0f;
    } else if (2.0f * below < -w) {
        boost = 0.0f;
    } else if (2.0f * below <= w) {
        const float t = below + 0.5f * w;
        boost = slope * t * t / (2.0f * w);
    } else {
        boost = slope * below;
    }
    boost = std::min(boost, p.maxBoostDb);

    const float x = std::clamp((levelDb - p.noiseFloorDb) / kNoiseTaperDb, 0.0f, 1.0f);
    return boost * (x * x * (3.0f - 2.0f * x));
}

// Total gain in dB for a detector level, including makeup. Zero when the compressor is off.
inline float staticGainDb(float levelDb, const Params& p) noexcept {
    if (!p.compEnabled) {
        return 0.0f;
    }
    return downwardGainDb(levelDb, p) + upwardGainDb(levelDb, p) + p.makeupDb;
}

} // namespace lsq::curve
