#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lsq/params.hpp"
#include "lsq/true_peak.hpp"

namespace lsq {

// Look-ahead true-peak limiter on interleaved stereo.
//
// For every sample the gain needed to stay under the ceiling is computed from an 8x oversampled
// peak estimate. A sliding-window minimum over the look-ahead time, a release envelope that is
// never allowed above that minimum, and a box filter of the look-ahead length turn it into a
// smooth gain. The audio is delayed by latencyFrames() so the gain is always in place before the
// peak arrives; the ceiling is therefore met by construction, not by clipping.
class Limiter {
public:
    static constexpr double kLookaheadMs = 5.0;
    static constexpr int kInterpolatorTaps = 32;

    // Allocates. Call off the audio thread.
    void prepare(double sampleRate, double lookaheadMs = kLookaheadMs);
    void reset() noexcept;

    // Samples of delay between input and output.
    int latencyFrames() const noexcept { return delay_; }

    // In-place on interleaved stereo. Returns the smallest linear gain applied (1 = untouched).
    float process(float* lr, std::size_t frames, const Params& p) noexcept;

private:
    double sampleRate_ = 48000.0;
    int lookahead_ = 240; // L: box filter length
    int window_ = 241;    // L + 1: sliding minimum window
    int delay_ = 0;       // detector delay + L - 1

    InterpolatorTaps taps_;
    TruePeakChannel peak_[2];

    // Audio delay line (ring of delay_ + 1 stereo frames)
    std::vector<float> audioDelay_;
    std::size_t audioPos_ = 0;

    // Sliding-window minimum of the required gain (monotone deque in a ring)
    std::vector<float> dqValue_;
    std::vector<std::uint64_t> dqIndex_;
    std::size_t dqHead_ = 0;
    std::size_t dqCount_ = 0;
    std::uint64_t n_ = 0;

    // Release envelope and its running box average
    std::vector<float> envHistory_;
    std::size_t envPos_ = 0;
    double envSum_ = 0.0;
    float envPrev_ = 1.0f;
};

} // namespace lsq
