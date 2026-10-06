#include "lsq/true_peak.hpp"

#include <algorithm>
#include <cmath>

namespace lsq {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order 0 (power series).
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 64; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-18 * sum) {
            break;
        }
    }
    return sum;
}

} // namespace

int InterpolatorTaps::factorForSampleRate(double sampleRate) noexcept {
    if (sampleRate < 88200.0) {
        return 8;
    }
    if (sampleRate < 176400.0) {
        return 4;
    }
    return 1;
}

InterpolatorTaps InterpolatorTaps::design(int factor, int taps, double kaiserBeta) {
    InterpolatorTaps t;
    t.factor = std::clamp(factor, 1, kMaxFactor); // push() keeps one accumulator per phase
    t.taps = std::max(2, taps & ~1);              // even, so the centre falls on a sample
    const int F = t.factor;
    const int K = t.taps;
    t.transposed.assign(static_cast<std::size_t>((K + 1) * F), 0.0f);

    // Prototype h[m], m = 0 .. F*K, centred on c = F*K/2.
    const int total = F * K;
    const double c = 0.5 * total;
    const double i0beta = besselI0(kaiserBeta);
    std::vector<double> h(static_cast<std::size_t>(total + 1));
    for (int m = 0; m <= total; ++m) {
        const double x = (static_cast<double>(m) - c) / static_cast<double>(F);
        const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = (static_cast<double>(m) - c) / c;
        const double w = besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
        h[static_cast<std::size_t>(m)] = sinc * w;
    }

    for (int p = 0; p < F; ++p) {
        // Phase p uses prototype taps h[p + F*i], i = 0 .. K; normalise each phase to unit DC gain.
        double sum = 0.0;
        for (int i = 0; i <= K; ++i) {
            const int m = p + F * i;
            if (m <= total) {
                sum += h[static_cast<std::size_t>(m)];
            }
        }
        for (int i = 0; i <= K; ++i) {
            const int m = p + F * i;
            if (m > total) {
                continue;
            }
            // x[n - i] sits at index K - i of the oldest-first window.
            const int k = K - i;
            t.transposed[static_cast<std::size_t>(k * F + p)] =
                static_cast<float>(h[static_cast<std::size_t>(m)] / sum);
        }
    }
    return t;
}

void TruePeakChannel::prepare(const InterpolatorTaps* taps) {
    taps_ = taps;
    len_ = static_cast<std::size_t>(taps->taps + 1);
    delay_ = taps->taps / 2;
    buf_.assign(2 * len_, 0.0f);
    pos_ = 0;
}

void TruePeakChannel::reset() noexcept {
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    pos_ = 0;
}

float TruePeakChannel::push(float x) noexcept {
    buf_[pos_] = x;
    buf_[pos_ + len_] = x;
    if (++pos_ == len_) {
        pos_ = 0;
    }
    // Oldest-first window of the len_ most recent samples; the newest is w[len_ - 1].
    const float* w = buf_.data() + pos_;
    float peak = std::fabs(w[len_ - 1 - static_cast<std::size_t>(delay_)]); // phase 0 = identity

    const int F = taps_->factor;
    if (F > 1) {
        float acc[InterpolatorTaps::kMaxFactor] = {};
        const float* tp = taps_->transposed.data();
        for (std::size_t k = 0; k < len_; ++k) {
            const float v = w[k];
            const float* row = tp + k * static_cast<std::size_t>(F);
            for (int p = 1; p < F; ++p) {
                acc[p] += row[p] * v;
            }
        }
        for (int p = 1; p < F; ++p) {
            peak = std::max(peak, std::fabs(acc[p]));
        }
    }
    return peak;
}

} // namespace lsq
