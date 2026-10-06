#include "lsq/limiter.hpp"

#include <algorithm>
#include <cmath>

#include "lsq/db.hpp"
#include "lsq/rt_utils.hpp"

namespace lsq {

void Limiter::prepare(double sampleRate, double lookaheadMs) {
    sampleRate_ = sampleRate;
    lookahead_ = std::max(2, static_cast<int>(std::lround(lookaheadMs * 1e-3 * sampleRate)));
    window_ = lookahead_ + 1;

    taps_ = InterpolatorTaps::design(InterpolatorTaps::factorForSampleRate(sampleRate),
                                     kInterpolatorTaps);
    peak_[0].prepare(&taps_);
    peak_[1].prepare(&taps_);

    delay_ = peak_[0].delay() + lookahead_ - 1;
    audioDelay_.assign(2 * static_cast<std::size_t>(delay_ + 1), 0.0f);

    const auto cap = static_cast<std::size_t>(window_ + 1);
    dqValue_.assign(cap, 1.0f);
    dqIndex_.assign(cap, 0);
    envHistory_.assign(static_cast<std::size_t>(lookahead_), 1.0f);
    reset();
}

void Limiter::reset() noexcept {
    peak_[0].reset();
    peak_[1].reset();
    std::fill(audioDelay_.begin(), audioDelay_.end(), 0.0f);
    audioPos_ = 0;
    dqHead_ = 0;
    dqCount_ = 0;
    n_ = 0;
    std::fill(envHistory_.begin(), envHistory_.end(), 1.0f);
    envPos_ = 0;
    envSum_ = static_cast<double>(lookahead_);
    envPrev_ = 1.0f;
}

float Limiter::process(float* lr, std::size_t frames, const Params& p) noexcept {
    const float ceiling = dbToLin(p.ceilingDb);
    const float releaseCoef =
        static_cast<float>(timeConstCoef(static_cast<double>(p.limiterReleaseMs),
                                         sampleRate_)); // fraction of the gap closed per sample
    const std::size_t cap = dqValue_.size();
    const std::size_t delayLen = static_cast<std::size_t>(delay_ + 1);
    const double invL = 1.0 / static_cast<double>(lookahead_);

    float minGain = 1.0f;

    for (std::size_t f = 0; f < frames; ++f) {
        const float x0 = lr[2 * f];
        const float x1 = lr[2 * f + 1];

        // 1. Gain required by the peak around the sample that just became visible.
        const float q = std::max(peak_[0].push(x0), peak_[1].push(x1));
        const float required = q > ceiling ? ceiling / q : 1.0f;

        // 2. Minimum of the required gain over the last `window_` samples.
        while (dqCount_ > 0 && dqValue_[(dqHead_ + dqCount_ - 1) % cap] >= required) {
            --dqCount_;
        }
        const std::size_t slot = (dqHead_ + dqCount_) % cap;
        dqValue_[slot] = required;
        dqIndex_[slot] = n_;
        ++dqCount_;
        while (dqIndex_[dqHead_] + static_cast<std::uint64_t>(window_) <= n_) {
            dqHead_ = (dqHead_ + 1) % cap;
            --dqCount_;
        }
        const float windowMin = dqValue_[dqHead_];

        // 3. Release envelope: drops instantly to the minimum, recovers exponentially.
        const float recovering = envPrev_ + (1.0f - envPrev_) * releaseCoef;
        const float env = std::min(windowMin, recovering);
        envPrev_ = env;

        // 4. Box average over the look-ahead length.
        envSum_ += static_cast<double>(env) - static_cast<double>(envHistory_[envPos_]);
        envHistory_[envPos_] = env;
        if (++envPos_ == envHistory_.size()) {
            envPos_ = 0;
        }
        const float gain = static_cast<float>(envSum_ * invL);
        minGain = std::min(minGain, gain);

        // 5. Delayed audio times gain. The slot after the write position holds the oldest sample.
        float* slotPtr = &audioDelay_[2 * audioPos_];
        slotPtr[0] = x0;
        slotPtr[1] = x1;
        audioPos_ = audioPos_ + 1 == delayLen ? 0 : audioPos_ + 1;
        const float* old = &audioDelay_[2 * audioPos_];
        lr[2 * f] = std::clamp(old[0] * gain, -ceiling, ceiling);
        lr[2 * f + 1] = std::clamp(old[1] * gain, -ceiling, ceiling);

        ++n_;
    }
    return minGain;
}

} // namespace lsq
