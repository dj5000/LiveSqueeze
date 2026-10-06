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

// Linear interpolation between two parameter sets (t in [0, 1]). Booleans switch at t = 0.5.
Params blendParams(const Params& a, const Params& b, float t) noexcept;

} // namespace lsq
