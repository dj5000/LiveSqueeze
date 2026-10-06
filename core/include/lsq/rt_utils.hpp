#pragma once

// Small helpers that are safe to use on the audio thread: no allocation, no locks.

#include <cmath>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#define LSQ_HAVE_MXCSR 1
#endif

namespace lsq {

// Flush denormals to zero for the lifetime of the object, then restore the previous floating point
// state. Construct it at the top of every audio callback.
class ScopedFtz {
public:
    ScopedFtz() noexcept {
#if defined(LSQ_HAVE_MXCSR)
        saved_ = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned int>(saved_) | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
        std::uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (std::uint64_t{1} << 24); // FZ
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#endif
    }
    ~ScopedFtz() {
#if defined(LSQ_HAVE_MXCSR)
        _mm_setcsr(static_cast<unsigned int>(saved_));
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
        asm volatile("msr fpcr, %0" ::"r"(saved_));
#endif
    }
    ScopedFtz(const ScopedFtz&) = delete;
    ScopedFtz& operator=(const ScopedFtz&) = delete;

private:
    std::uint64_t saved_ = 0;
};

// One-pole smoothing coefficient for a 1/e time constant of `ms` milliseconds.
// y += coef * (target - y)
inline double timeConstCoef(double ms, double sampleRate) noexcept {
    if (ms <= 0.0) {
        return 1.0;
    }
    return 1.0 - std::exp(-1.0 / (ms * 1e-3 * sampleRate));
}

// Linear ramp to a target over a fixed number of samples. Stepped once per sample, so the result
// does not depend on how the audio is split into blocks.
class Ramp {
public:
    void reset(float v) noexcept {
        cur_ = tgt_ = v;
        step_ = 0.0f;
        left_ = 0;
    }

    void setTarget(float t, int samples) noexcept {
        if (t == tgt_) {
            return;
        }
        tgt_ = t;
        if (samples <= 0) {
            cur_ = t;
            left_ = 0;
            return;
        }
        step_ = (t - cur_) / static_cast<float>(samples);
        left_ = samples;
    }

    float next() noexcept {
        if (left_ > 0) {
            cur_ += step_;
            if (--left_ == 0) {
                cur_ = tgt_;
            }
        }
        return cur_;
    }

    bool active() const noexcept { return left_ > 0; }
    float current() const noexcept { return cur_; }
    float target() const noexcept { return tgt_; }

private:
    float cur_ = 0.0f;
    float tgt_ = 0.0f;
    float step_ = 0.0f;
    int left_ = 0;
};

} // namespace lsq
