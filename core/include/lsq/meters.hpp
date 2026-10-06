#pragma once

#include <type_traits>

#include "lsq/db.hpp"

namespace lsq {

// One meter reading, produced by the audio thread about 50 times per second.
struct MeterFrame {
    float inPeakDb = kSilenceDb;        // peak of the stereo signal entering the chain
    float outPeakDb = kSilenceDb;       // peak of the signal leaving the chain
    float compGainDb = 0.0f;            // most negative compressor gain in the interval (reduction)
    float compMaxGainDb = 0.0f;         // most positive compressor gain in the interval (boost)
    float limiterGainDb = 0.0f;         // most negative limiter gain in the interval
    float detectorLevelDb = kSilenceDb; // detector level at the end of the interval
};

static_assert(std::is_trivially_copyable_v<MeterFrame>);

} // namespace lsq
