#pragma once

#include <cstddef>
#include <vector>

#include "lsq/compressor.hpp"
#include "lsq/limiter.hpp"
#include "lsq/meters.hpp"
#include "lsq/params.hpp"
#include "lsq/rt_utils.hpp"
#include "lsq/spsc_ring.hpp"

namespace lsq {

// Everything after the downmix: compressor -> limiter -> bypass crossfade -> output trim -> safety
// clamp. Works in place on interleaved stereo and is independent of how the audio is chunked.
//
// Bypass mixes in a copy of the input delayed by exactly the limiter latency, so toggling it does
// not comb-filter or change timing. In bypass the chain is a delay plus the safety clamp.
class DynamicsChain {
public:
    // Allocates. `maxBlock` is the largest chunk handled at once (longer calls are split).
    void prepare(double sampleRate, std::size_t maxBlock = 1024);
    void reset() noexcept;

    // Real-time safe. Non-finite input samples are replaced by silence.
    void process(float* lr, std::size_t frames, const Params& p) noexcept;

    // Frames of delay between input and output.
    int latencyFrames() const noexcept { return limiter_.latencyFrames(); }

    // Meter frames, about every 20 ms of audio. Call from one other thread.
    bool popMeter(MeterFrame& out) noexcept { return meters_.pop(out); }

    double sampleRate() const noexcept { return sampleRate_; }

private:
    void processChunk(float* lr, std::size_t frames, const Params& p) noexcept;

    double sampleRate_ = 48000.0;
    std::size_t maxBlock_ = 1024;
    Compressor compressor_;
    Limiter limiter_;

    std::vector<float> dryDelay_; // ring of latency + 1 stereo frames
    std::size_t dryPos_ = 0;
    std::vector<float> dryOut_; // delayed dry signal for the current chunk

    Ramp bypassRamp_;
    Ramp trimRamp_;
    int rampSamples_ = 960;

    // Meter accumulation
    SpscRing<MeterFrame, 64> meters_;
    std::size_t meterInterval_ = 960;
    std::size_t meterCount_ = 0;
    float accInPeak_ = 0.0f;
    float accOutPeak_ = 0.0f;
    float accCompMin_ = 0.0f;
    float accCompMax_ = 0.0f;
    float accLimMin_ = 1.0f;
};

} // namespace lsq
