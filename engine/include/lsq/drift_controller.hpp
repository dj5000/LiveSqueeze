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

    // `deadbandFrames`: differences from the target up to this size are not corrected. Used when
    // both streams run in one cycle: the order in which their callbacks run can change, and the
    // fill seen at the start of the playback callback then moves by one capture block without
    // anything having gone wrong.
    void configure(double captureRate, double targetFrames, double deadbandFrames = 0.0) noexcept {
        rate_ = captureRate;
        target_ = targetFrames;
        deadband_ = std::max(deadbandFrames, 0.0);
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
        double errFrames = smoothed_ - target_;
        if (errFrames > deadband_) {
            errFrames -= deadband_;
        } else if (errFrames < -deadband_) {
            errFrames += deadband_;
        } else {
            errFrames = 0.0;
        }
        const double errMs = errFrames / rate_ * 1000.0;

        // PI with conditional integration: while the output is saturated and the error would push
        // it further into saturation, stop integrating. Otherwise the integral keeps growing and
        // the controller stays pinned at its limit long after the error has gone.
        const double proportional = kKp * errMs;
        const double unclamped = proportional + kKi * integral_;
        const bool pushingFurther =
            (unclamped > kMaxTrimPpm && errMs > 0.0) || (unclamped < -kMaxTrimPpm && errMs < 0.0);
        if (!pushingFurther) {
            integral_ += errMs * dtSeconds;
        }
        integral_ = std::clamp(integral_, -kMaxTrimPpm / kKi, kMaxTrimPpm / kKi);
        trim_ = std::clamp(proportional + kKi * integral_, -kMaxTrimPpm, kMaxTrimPpm);
        return trim_;
    }

    double trimPpm() const noexcept { return trim_; }
    double smoothedFill() const noexcept { return smoothed_; }
    double target() const noexcept { return target_; }

private:
    double rate_ = 48000.0;
    double target_ = 0.0;
    double deadband_ = 0.0;
    double smoothed_ = 0.0;
    double integral_ = 0.0;
    double trim_ = 0.0;
};

} // namespace lsq
