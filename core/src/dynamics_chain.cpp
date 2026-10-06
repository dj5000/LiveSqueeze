#include "lsq/dynamics_chain.hpp"

#include <algorithm>
#include <cmath>

#include "lsq/db.hpp"

namespace lsq {

void DynamicsChain::prepare(double sampleRate, std::size_t maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::max<std::size_t>(maxBlock, 1);
    compressor_.prepare(sampleRate);
    limiter_.prepare(sampleRate);

    dryDelay_.assign(2 * static_cast<std::size_t>(limiter_.latencyFrames() + 1), 0.0f);
    dryOut_.assign(2 * maxBlock_, 0.0f);

    rampSamples_ = std::max(1, static_cast<int>(sampleRate * 0.020));
    meterInterval_ = std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate * 0.020));
    reset();
}

void DynamicsChain::reset() noexcept {
    compressor_.reset();
    limiter_.reset();
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0f);
    dryPos_ = 0;
    bypassRamp_.reset(0.0f);
    trimRamp_.reset(1.0f);
    meterCount_ = 0;
    accInPeak_ = accOutPeak_ = 0.0f;
    accCompMin_ = accCompMax_ = 0.0f;
    accLimMin_ = 1.0f;
}

void DynamicsChain::process(float* lr, std::size_t frames, const Params& p) noexcept {
    ScopedFtz ftz;
    while (frames > 0) {
        const std::size_t n = std::min(frames, maxBlock_);
        processChunk(lr, n, p);
        lr += 2 * n;
        frames -= n;
    }
}

void DynamicsChain::processChunk(float* lr, std::size_t frames, const Params& p) noexcept {
    // Sanitize and measure the input.
    float inPeak = 0.0f;
    for (std::size_t i = 0; i < 2 * frames; ++i) {
        float v = lr[i];
        if (!std::isfinite(v)) {
            v = 0.0f;
        }
        lr[i] = v;
        inPeak = std::max(inPeak, std::fabs(v));
    }

    // Delay-matched dry copy for bypass.
    const std::size_t dryLen = dryDelay_.size() / 2;
    for (std::size_t f = 0; f < frames; ++f) {
        float* slot = &dryDelay_[2 * dryPos_];
        slot[0] = lr[2 * f];
        slot[1] = lr[2 * f + 1];
        dryPos_ = dryPos_ + 1 == dryLen ? 0 : dryPos_ + 1;
        const float* old = &dryDelay_[2 * dryPos_];
        dryOut_[2 * f] = old[0];
        dryOut_[2 * f + 1] = old[1];
    }

    const CompStats cs = compressor_.process(lr, frames, p);
    const float limMin = limiter_.process(lr, frames, p);

    // Crossfade to the dry path, apply the output trim, and clamp as a last resort.
    bypassRamp_.setTarget(p.bypass ? 1.0f : 0.0f, rampSamples_);
    trimRamp_.setTarget(dbToLin(p.outTrimDb), rampSamples_);
    float outPeak = 0.0f;
    for (std::size_t f = 0; f < frames; ++f) {
        const float b = bypassRamp_.next();
        const float t = trimRamp_.next();
        for (std::size_t c = 0; c < 2; ++c) {
            const float wet = lr[2 * f + c];
            const float dry = dryOut_[2 * f + c];
            const float v = std::clamp((wet * (1.0f - b) + dry * b) * t, -1.0f, 1.0f);
            lr[2 * f + c] = v;
            outPeak = std::max(outPeak, std::fabs(v));
        }
    }

    // Meters.
    accInPeak_ = std::max(accInPeak_, inPeak);
    accOutPeak_ = std::max(accOutPeak_, outPeak);
    accCompMin_ = std::min(accCompMin_, cs.minGainDb);
    accCompMax_ = std::max(accCompMax_, cs.maxGainDb);
    accLimMin_ = std::min(accLimMin_, limMin);
    meterCount_ += frames;
    if (meterCount_ >= meterInterval_) {
        MeterFrame m;
        m.inPeakDb = linToDb(accInPeak_);
        m.outPeakDb = linToDb(accOutPeak_);
        m.compGainDb = accCompMin_;
        m.compMaxGainDb = accCompMax_;
        m.limiterGainDb = linToDb(accLimMin_);
        m.detectorLevelDb = cs.levelDb;
        meters_.push(m);
        meterCount_ = 0;
        accInPeak_ = accOutPeak_ = 0.0f;
        accCompMin_ = accCompMax_ = 0.0f;
        accLimMin_ = 1.0f;
    }
}

} // namespace lsq
