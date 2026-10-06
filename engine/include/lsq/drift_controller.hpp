#pragma once

#include <algorithm>
#include <cmath>

namespace lsq {

// Keeps the capture ring near a target fill level by trimming the resampling ratio.
//
// The fill level is smoothed (it jitters by up to a callback period), the error in milliseconds
// drives a PI controller, and the output is a ratio trim in parts per million, clamped to
// +/-2000 ppm. A positive trim consumes input faster, which lowers the fill.
//
// With the gains below, the loop is critically damped with a natural frequency of about
// 0.19 rad/s: it settles in roughly 20 s and a clock offset of 100 ppm moves the fill by
// well under a millisecond.
class DriftController {
public:
    static constexpr double kKp = 380.0;          // ppm per millisecond of error
    static constexpr double kKi = 36.0;           // ppm per millisecond-second of error
    static constexpr double kMaxTrimPpm = 2000.0; // 0.2%, about 3.5 cents: inaudible
    static constexpr double kSmoothingSeconds = 1.5;

    void configure(double captureRate, double targetFrames) noexcept {
        rate_ = captureRate;
        target_ = targetFrames;
        reset();
    }

    // Starts from "on target": the first measurements are then judged against it rather than
    // being taken as the new normal.
    void reset() noexcept {
        smoothed_ = target_;
        integral_ = 0.0;
        trim_ = 0.0;
    }

    // Call once per playback callback with the ring fill (frames) at its start and the duration
    // of the block (seconds). Returns the new trim in ppm.
    double update(double fillFrames, double dtSeconds) noexcept {
        smoothed_ += (fillFrames - smoothed_) * (1.0 - std::exp(-dtSeconds / kSmoothingSeconds));
        const double errMs = (smoothed_ - target_) / rate_ * 1000.0;

        // Integrate, clamped so the integral term alone can never exceed the trim limit.
        integral_ =
            std::clamp(integral_ + errMs * dtSeconds, -kMaxTrimPpm / kKi, kMaxTrimPpm / kKi);
        trim_ = std::clamp(kKp * errMs + kKi * integral_, -kMaxTrimPpm, kMaxTrimPpm);
        return trim_;
    }

    double trimPpm() const noexcept { return trim_; }
    double smoothedFill() const noexcept { return smoothed_; }
    double target() const noexcept { return target_; }

private:
    double rate_ = 48000.0;
    double target_ = 0.0;
    double smoothed_ = 0.0;
    double integral_ = 0.0;
    double trim_ = 0.0;
};

} // namespace lsq
