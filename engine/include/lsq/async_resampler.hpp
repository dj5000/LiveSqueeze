#pragma once

#include <cstddef>
#include <vector>

namespace lsq {

// Stereo sample-rate converter with a continuously adjustable ratio. It converts between the
// capture and playback device rates and, by nudging the ratio by a few hundred ppm, absorbs the
// slow drift between their clocks.
//
// A Kaiser-windowed sinc kernel (24 zero crossings each side, 256 table entries per sample,
// linear interpolation) is used. `ratio` is the number of input frames consumed per output frame,
// i.e. inputRate / outputRate. The kernel is designed for the nominal ratio given to prepare();
// later ratios should stay within a fraction of a percent of it.
//
// The output is delayed by latencyFrames() input frames relative to the input.
class AsyncResampler {
public:
    static constexpr int kChannels = 2;
    static constexpr int kHalfTaps = 24;
    static constexpr int kTableRes = 256;

    // Allocates. `maxInFrames` is the most input ever supplied to one process() call.
    void prepare(double nominalRatio, std::size_t maxInFrames);
    void reset() noexcept;

    // Input frames that must be supplied to produce `outFrames` outputs at `ratio`.
    std::size_t inputFramesNeeded(std::size_t outFrames, double ratio) const noexcept;

    // Most outputs that can be produced if `inFrames` more input frames are supplied.
    std::size_t outputFramesPossible(std::size_t inFrames, double ratio) const noexcept;

    // Appends `inFrames` interleaved stereo frames and produces up to `outFrames` output frames.
    // Returns the number produced (less than outFrames only if too little input was supplied).
    std::size_t process(const float* in, std::size_t inFrames, float* out, std::size_t outFrames,
                        double ratio) noexcept;

    // Delay in input frames.
    int latencyFrames() const noexcept { return reach_; }

    // Largest number of input frames a process() call may be given (buffer limit).
    std::size_t maxInputFrames() const noexcept { return maxIn_; }

private:
    std::vector<float> table_;
    std::vector<float> buf_; // interleaved stereo history + pending input
    double support_ = 24.0;  // kernel half width in input frames
    int reach_ = 24;         // ceil(support_)
    std::size_t maxIn_ = 0;
    std::size_t len_ = 0; // valid frames in buf_
    double pos_ = 0.0;    // position of the next output, in buf_ frames
};

} // namespace lsq
