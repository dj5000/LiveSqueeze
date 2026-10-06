#pragma once

// Shared helpers for the core tests: signal generators, level measurement, and an independent
// true-peak oracle (long windowed-sinc, 16x oversampling) to check the limiter against.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "doctest.h"

#define CHECK_NEAR(a, b, tol)                                                                      \
    do {                                                                                           \
        const double lsq_a_ = static_cast<double>(a);                                              \
        const double lsq_b_ = static_cast<double>(b);                                              \
        CHECK_MESSAGE(std::fabs(lsq_a_ - lsq_b_) <= static_cast<double>(tol),                      \
                      lsq_a_ << " vs " << lsq_b_ << " (tolerance " << (tol) << ")");               \
    } while (0)

namespace lsqtest {

constexpr double kPi = 3.14159265358979323846;

// Number of global operator new calls so far (see alloc_counter.cpp).
std::size_t allocCount();

inline double toDb(double lin) {
    return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0;
}

// Deterministic noise in [-1, 1).
struct Rng {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    float next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<float>(static_cast<double>(s >> 11) / 4503599627370496.0 - 1.0);
    }
};

// Interleaved stereo sine, same signal in both channels. `ampDb` is the peak level in dBFS.
inline std::vector<float> sine(std::size_t frames, double freqHz, double ampDb, double sampleRate,
                               double phase = 0.0) {
    std::vector<float> v(2 * frames);
    const double amp = std::pow(10.0, ampDb / 20.0);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto s = static_cast<float>(
            amp * std::sin(2.0 * kPi * freqHz * static_cast<double>(i) / sampleRate + phase));
        v[2 * i] = s;
        v[2 * i + 1] = s;
    }
    return v;
}

inline std::vector<float> noise(std::size_t frames, float amp, std::uint64_t seed = 1) {
    Rng rng;
    rng.s ^= seed * 0x2545F4914F6CDD1Dull;
    std::vector<float> v(2 * frames);
    for (auto& x : v) {
        x = amp * rng.next();
    }
    return v;
}

inline double peakLin(const std::vector<float>& v, std::size_t from = 0) {
    double p = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) {
        p = std::max(p, static_cast<double>(std::fabs(v[i])));
    }
    return p;
}

// RMS in dB over frames [from, to) of an interleaved stereo signal (both channels).
inline double rmsDb(const std::vector<float>& v, std::size_t fromFrame, std::size_t toFrame) {
    double sum = 0.0;
    for (std::size_t i = 2 * fromFrame; i < 2 * toFrame; ++i) {
        sum += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    }
    return 10.0 * std::log10(std::max(sum / static_cast<double>(2 * (toFrame - fromFrame)), 1e-24));
}

inline bool allFinite(const std::vector<float>& v) {
    for (float x : v) {
        if (!std::isfinite(x)) {
            return false;
        }
    }
    return true;
}

// ---- True-peak oracle ---------------------------------------------------------------------

inline double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 80; ++k) {
        term *= (x * x * 0.25) / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
    }
    return sum;
}

// Windowed-sinc reconstruction filter bank: [phase][tap], 2*H taps, Kaiser beta 9.
struct OracleBank {
    static constexpr int kOversample = 16;
    static constexpr int kHalf = 48;
    std::vector<double> coef;
    OracleBank() : coef(static_cast<std::size_t>(kOversample * 2 * kHalf)) {
        const double i0 = besselI0(9.0);
        for (int j = 0; j < kOversample; ++j) {
            const double frac = static_cast<double>(j) / kOversample;
            for (int t = 0; t < 2 * kHalf; ++t) {
                const int k = t - kHalf + 1; // sample offset -H+1 .. H
                const double x = static_cast<double>(k) - frac;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = x / kHalf;
                const double w =
                    std::fabs(r) >= 1.0 ? 0.0 : besselI0(9.0 * std::sqrt(1.0 - r * r)) / i0;
                coef[static_cast<std::size_t>(j * 2 * kHalf + t)] = sinc * w;
            }
        }
    }
};

// True peak (linear) of one channel of an interleaved signal, ignoring the first and last
// `margin` frames where the reconstruction would see the edge of the buffer.
inline double oracleTruePeak(const std::vector<float>& x, int channels, int channel,
                             std::size_t margin = OracleBank::kHalf) {
    static const OracleBank bank;
    const std::size_t frames = x.size() / static_cast<std::size_t>(channels);
    if (frames < 2 * margin + 2) {
        return 0.0;
    }
    margin = std::max<std::size_t>(margin, OracleBank::kHalf);
    double peak = 0.0;
    for (std::size_t n = margin; n + margin < frames; ++n) {
        for (int j = 0; j < OracleBank::kOversample; ++j) {
            const double* c = &bank.coef[static_cast<std::size_t>(j * 2 * OracleBank::kHalf)];
            double acc = 0.0;
            for (int t = 0; t < 2 * OracleBank::kHalf; ++t) {
                const std::size_t idx = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n) +
                                                                 t - OracleBank::kHalf + 1);
                acc += c[t] * static_cast<double>(x[idx * static_cast<std::size_t>(channels) +
                                                    static_cast<std::size_t>(channel)]);
            }
            peak = std::max(peak, std::fabs(acc));
        }
    }
    return peak;
}

inline double oracleTruePeakDb(const std::vector<float>& x, int channels = 2,
                               std::size_t margin = OracleBank::kHalf) {
    double p = 0.0;
    for (int c = 0; c < channels; ++c) {
        p = std::max(p, oracleTruePeak(x, channels, c, margin));
    }
    return toDb(p);
}

} // namespace lsqtest
