#include "lsq/compressor.hpp"

#include <algorithm>
#include <cmath>

#include "lsq/gain_computer.hpp"
#include "lsq/rt_utils.hpp"

namespace lsq {
namespace {
constexpr double kDetectorMs = 10.0;
constexpr double kSidechainHpfHz = 80.0;
// A full-scale sine has a mean square of 0.5 (-3.01 dB); add this so it reads 0 dB.
constexpr float kSineCalibrationDb = 3.0103f;
constexpr float kLn10Over20 = 0.115129254649702f;
} // namespace

void Compressor::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    detCoef_ = timeConstCoef(kDetectorMs, sampleRate);
    const Biquad hp = Biquad::highpass(sampleRate, kSidechainHpfHz);
    hpf_[0] = hp;
    hpf_[1] = hp;
    reset();
}

void Compressor::reset() noexcept {
    hpf_[0].reset();
    hpf_[1].reset();
    msL_ = msR_ = 0.0;
    gainDb_ = 0.0;
}

CompStats Compressor::process(float* lr, std::size_t frames, const Params& p) noexcept {
    const double attack = timeConstCoef(static_cast<double>(p.attackMs), sampleRate_);
    const double release = timeConstCoef(static_cast<double>(p.releaseMs), sampleRate_);

    CompStats stats;
    stats.minGainDb = static_cast<float>(gainDb_);
    stats.maxGainDb = static_cast<float>(gainDb_);
    float levelDb = stats.levelDb;

    for (std::size_t f = 0; f < frames; ++f) {
        const double l = static_cast<double>(lr[2 * f]);
        const double r = static_cast<double>(lr[2 * f + 1]);

        // Detector (sidechain only; the audio path is untouched).
        const double sl = p.sidechainHpf ? hpf_[0].process(l) : l;
        const double sr = p.sidechainHpf ? hpf_[1].process(r) : r;
        msL_ += detCoef_ * (sl * sl - msL_);
        msR_ += detCoef_ * (sr * sr - msR_);
        const double power = p.linkMax ? std::max(msL_, msR_) : 0.5 * (msL_ + msR_);
        levelDb =
            10.0f * std::log10(std::max(static_cast<float>(power), 1e-12f)) + kSineCalibrationDb;

        // Gain: instant target from the curve, smoothed asymmetrically.
        const double target = static_cast<double>(curve::staticGainDb(levelDb, p));
        const double coef = target < gainDb_ ? attack : release;
        gainDb_ += coef * (target - gainDb_);

        const float g = std::exp(static_cast<float>(gainDb_) * kLn10Over20);
        lr[2 * f] = static_cast<float>(l) * g;
        lr[2 * f + 1] = static_cast<float>(r) * g;

        stats.minGainDb = std::min(stats.minGainDb, static_cast<float>(gainDb_));
        stats.maxGainDb = std::max(stats.maxGainDb, static_cast<float>(gainDb_));
    }
    stats.levelDb = levelDb;
    return stats;
}

} // namespace lsq
