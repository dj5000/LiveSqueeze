#pragma once

#include <cstddef>
#include <vector>

namespace lsq {

// Polyphase windowed-sinc interpolator used to estimate inter-sample (true) peaks. It is used on
// the limiter's detection path only; the audio itself is never oversampled.
struct InterpolatorTaps {
    static constexpr int kMaxFactor = 8;

    int factor = 1; // oversampling factor F (phases per input sample), at most kMaxFactor
    int taps = 0;   // K: input samples per phase; the group delay is K/2 input samples
    // transposed[k * factor + p] is the weight of the k-th oldest of the K+1 most recent samples
    // for phase p. Phase 0 is the identity (the sample K/2 samples ago).
    std::vector<float> transposed;

    // Picks the factor from the sample rate (8x below 88.2 kHz, 4x below 176.4 kHz, else 1x).
    static int factorForSampleRate(double sampleRate) noexcept;

    // Kaiser-windowed sinc, beta chosen for a flat passband rather than deep stopband rejection:
    // image energy only makes the estimate more conservative.
    static InterpolatorTaps design(int factor, int taps, double kaiserBeta = 5.0);
};

// Tracks the true peak of one channel. After push(x[n]) the result covers the signal on
// [n - K/2, n - K/2 + 1), i.e. it is delayed by K/2 samples relative to the input.
class TruePeakChannel {
public:
    void prepare(const InterpolatorTaps* taps);
    void reset() noexcept;

    // Allocation free. Returns max |value| over the F points of the covered interval.
    float push(float x) noexcept;

    int delay() const noexcept { return delay_; }

private:
    const InterpolatorTaps* taps_ = nullptr;
    std::vector<float> buf_; // two copies of the history so any window is contiguous
    std::size_t pos_ = 0;
    std::size_t len_ = 0;
    int delay_ = 0;
};

} // namespace lsq
