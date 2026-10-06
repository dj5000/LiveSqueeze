#pragma once

#include <cstddef>
#include <string_view>

#include "lsq/params.hpp"

namespace lsq {

enum class Preset {
    MovieNight,    // default: tame explosions, keep dialogue, lift whispers
    LateNight,     // stronger squeeze for low volume listening
    DialogueBoost, // pushes the center channel forward
    GentleLeveler, // light downward compression only
    LimiterOnly,   // just the downmix and the peak limiter
};

inline constexpr std::size_t kPresetCount = 5;

const char* presetName(Preset p) noexcept; // "Movie / Night"
const char* presetKey(Preset p) noexcept;  // "movie-night"
bool presetFromKey(std::string_view key, Preset& out) noexcept;
Params presetParams(Preset p) noexcept;

// The "strength" slider of the simple view, 0 to 100, as a full set of parameters:
//   0          only the downmix and the peak limiter
//   0 to 50    the amount of compression grows smoothly (ratios from 1:1, the boost from 0 dB)
//   50         Movie / Night
//   50 to 100  towards Late night
// The user's volume trim, bypass switch and LFE settings are not part of it.
Params strengthParams(float percent) noexcept;

// Linear interpolation between two parameter sets (t in [0, 1]). Booleans switch at t = 0.5.
Params blendParams(const Params& a, const Params& b, float t) noexcept;

} // namespace lsq
