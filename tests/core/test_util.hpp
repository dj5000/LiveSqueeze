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

// ---- Sine fit -----------------------------------------------------------------------------

struct SineFit {
    double amplitude = 0.0;  // fitted peak amplitude
    double residualDb = 0.0; // residual RMS relative to the fitted sine's RMS (lower is purer)
};

// Least-squares fit of a sine of known frequency (cycles per sample) plus a DC offset to one
// channel of an interleaved signal over frames [from, to).
inline SineFit fitSine(const std::vector<float>& x, int channels, int channel, std::size_t from,
                       std::size_t to, double freq) {
    const auto nch = static_cast<std::size_t>(channels);
    double scc = 0, sss = 0, scs = 0, sc = 0, ss = 0, sxc = 0, sxs = 0, sx = 0;
    const double n = static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i) {
        const double w = 2.0 * kPi * freq * static_cast<double>(i);
        const double c = std::cos(w);
        const double s = std::sin(w);
        const double v = static_cast<double>(x[i * nch + static_cast<std::size_t>(channel)]);
        scc += c * c;
        sss += s * s;
        scs += c * s;
        sc += c;
        ss += s;
        sxc += v * c;
        sxs += v * s;
        sx += v;
    }
    // Solve the 3x3 normal equations [scc scs sc; scs sss ss; sc ss n] * [a b d] = [sxc sxs sx].
    double m[3][4] = {{scc, scs, sc, sxc}, {scs, sss, ss, sxs}, {sc, ss, n, sx}};
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r) {
            if (std::fabs(m[r][col]) > std::fabs(m[piv][col])) {
                piv = r;
            }
        }
        for (int k = 0; k < 4; ++k) {
            std::swap(m[col][k], m[piv][k]);
        }
        for (int r = 0; r < 3; ++r) {
            if (r != col) {
                const double f = m[r][col] / m[col][col];
                for (int k = col; k < 4; ++k) {
                    m[r][k] -= f * m[col][k];
                }
            }
        }
    }
    const double a = m[0][3] / m[0][0];
    const double b = m[1][3] / m[1][1];
    const double d = m[2][3] / m[2][2];
    double res = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double w = 2.0 * kPi * freq * static_cast<double>(i);
        const double v = static_cast<double>(x[i * nch + static_cast<std::size_t>(channel)]);
        const double e = v - (a * std::cos(w) + b * std::sin(w) + d);
        res += e * e;
    }
    SineFit f;
    f.amplitude = std::sqrt(a * a + b * b);
    const double fitPower = 0.5 * f.amplitude * f.amplitude;
    f.residualDb = 10.0 * std::log10(std::max(res / n, 1e-30) / std::max(fitPower, 1e-30));
    return f;
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
