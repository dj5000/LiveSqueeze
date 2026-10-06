#pragma once

#include <cstddef>

#include "lsq/biquad.hpp"
#include "lsq/params.hpp"

namespace lsq {

struct CompStats {
    float minGainDb = 0.0f;  // most negative gain applied during the call
    float maxGainDb = 0.0f;  // most positive gain applied during the call
    float levelDb = -120.0f; // detector level after the last frame
};

// Stereo-linked feed-forward compressor with downward compression, capped upward boost and a
// noise floor (see gain_computer.hpp for the curve). Zero latency.
//
// The detector is the power average of both channels (or the louder one) over about 10 ms,
// calibrated so a full-scale sine reads 0 dB. One gain is applied to both channels, so the stereo
// image does not move. The target gain is smoothed with the attack time when it falls and the
// release time when it rises.
class Compressor {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // In-place on interleaved stereo.
    CompStats process(float* lr, std::size_t frames, const Params& p) noexcept;

    float currentGainDb() const noexcept { return static_cast<float>(gainDb_); }

private:
    double sampleRate_ = 48000.0;
    double detCoef_ = 0.0;
    Biquad hpf_[2];
    double msL_ = 0.0;
    double msR_ = 0.0;
    double gainDb_ = 0.0;
};

} // namespace lsq
