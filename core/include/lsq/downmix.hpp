#pragma once

#include <cstddef>

#include "lsq/channel_map.hpp"
#include "lsq/params.hpp"
#include "lsq/rt_utils.hpp"

namespace lsq {

// Converts an interleaved N-channel stream to interleaved stereo (Lo/Ro, ITU-R BS.775 style).
//
//   L = FL + c*FC + s*(BL + SL) + lfe*LFE      c = 0.707 * centerGain, s = 0.707 * surroundGain
//   R = FR + c*FC + s*(BR + SR) + lfe*LFE
//
// A mono source is copied to both sides at unity. Coefficient changes are ramped over 30 ms.
class Downmixer {
public:
    // Allocation free; call from any thread before processing starts.
    void prepare(const ChannelMap& input, double sampleRate) noexcept;

    // `in` holds `frames` interleaved frames of map().n channels, `outLR` receives 2*frames values.
    void process(const float* in, float* outLR, std::size_t frames, const Params& p) noexcept;

    const ChannelMap& map() const noexcept { return map_; }

    // The target coefficients for a map and parameter set (exposed for tests and the GUI).
    static void computeCoefficients(const ChannelMap& map, const Params& p, float* toLeft,
                                    float* toRight) noexcept;

private:
    ChannelMap map_{};
    int rampSamples_ = 1440;
    bool initialized_ = false;
    Ramp left_[kMaxChannels];
    Ramp right_[kMaxChannels];
};

} // namespace lsq
