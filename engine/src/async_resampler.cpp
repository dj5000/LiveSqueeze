#include "lsq/async_resampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsq {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCutoff = 0.46; // fraction of the (lower) Nyquist rate kept
constexpr double kKaiserBeta = 9.0;

double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 80; ++k) {
        term *= (x * x * 0.25) / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-18 * sum) {
            break;
        }
    }
    return sum;
}

} // namespace

void AsyncResampler::prepare(double nominalRatio, std::size_t maxInFrames) {
    nominalRatio = std::clamp(nominalRatio, 0.1, 8.0);
    support_ = kHalfTaps * std::max(1.0, nominalRatio);
    reach_ = static_cast<int>(std::ceil(support_));
    maxIn_ = maxInFrames;

    // Low-pass at the lower of the two Nyquist rates, in cycles per input sample.
    const double fc = kCutoff * std::min(1.0, 1.0 / nominalRatio);
    const std::size_t entries = static_cast<std::size_t>(reach_ + 1) * kTableRes + 2;
    table_.assign(entries, 0.0f);
    const double i0 = besselI0(kKaiserBeta);
    for (std::size_t i = 0; i < entries; ++i) {
        const double d = static_cast<double>(i) / kTableRes;
        if (d >= support_) {
            break; // zero beyond the support
        }
        const double x = 2.0 * fc * d;
        const double sinc = x < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = d / support_;
        const double w = besselI0(kKaiserBeta * std::sqrt(1.0 - r * r)) / i0;
        table_[i] = static_cast<float>(2.0 * fc * sinc * w);
    }

    buf_.assign((maxInFrames + 2 * static_cast<std::size_t>(reach_) + 16) * kChannels, 0.0f);
    reset();
}

void AsyncResampler::reset() noexcept {
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    len_ = static_cast<std::size_t>(reach_);
    pos_ = static_cast<double>(reach_);
}

std::size_t AsyncResampler::inputFramesNeeded(std::size_t outFrames, double ratio) const noexcept {
    if (outFrames == 0) {
        return 0;
    }
    const double last = pos_ + static_cast<double>(outFrames - 1) * ratio + 1e-9;
    const double need = std::floor(last) + static_cast<double>(reach_) + 1.0;
    const double have = static_cast<double>(len_);
    return need > have ? static_cast<std::size_t>(need - have) : 0;
}

std::size_t AsyncResampler::outputFramesPossible(std::size_t inFrames,
                                                 double ratio) const noexcept {
    const double t =
        (static_cast<double>(len_ + inFrames) - static_cast<double>(reach_) - pos_) / ratio;
    return t > 0.0 ? static_cast<std::size_t>(std::ceil(t)) : 0;
}

std::size_t AsyncResampler::process(const float* in, std::size_t inFrames, float* out,
                                    std::size_t outFrames, double ratio) noexcept {
    // Append the new input (clamped to the buffer, which prepare() sized for maxInFrames).
    const std::size_t room = buf_.size() / kChannels - len_;
    inFrames = std::min(inFrames, room);
    if (inFrames > 0) {
        std::memcpy(&buf_[len_ * kChannels], in, inFrames * kChannels * sizeof(float));
        len_ += inFrames;
    }

    const std::size_t n = std::min(outFrames, outputFramesPossible(0, ratio));
    const float* tbl = table_.data();
    const int reach = reach_;

    for (std::size_t j = 0; j < n; ++j) {
        const auto i0 = static_cast<std::size_t>(pos_);
        const float f = static_cast<float>(pos_ - static_cast<double>(i0));

        // Taps on the left (input frames i0, i0-1, ...) are at distance f, f+1, ...; taps on the
        // right (i0+1, i0+2, ...) at 1-f, 2-f, ... Within each side the table index advances by a
        // whole kTableRes per tap, so the interpolation weight is shared by all taps of a side.
        const float xl = f * kTableRes;
        const int il = static_cast<int>(xl);
        const float wl = xl - static_cast<float>(il);
        const float xr = (1.0f - f) * kTableRes;
        const int ir = static_cast<int>(xr);
        const float wr = xr - static_cast<float>(ir);

        float a0 = 0.0f;
        float a1 = 0.0f;
        const float* left = &buf_[i0 * kChannels];
        const float* right = left + kChannels;
        for (int k = 0; k < reach; ++k) {
            const int off = k * kTableRes;
            const float kl = tbl[il + off] + wl * (tbl[il + off + 1] - tbl[il + off]);
            const float kr = tbl[ir + off] + wr * (tbl[ir + off + 1] - tbl[ir + off]);
            a0 += kl * left[-k * kChannels] + kr * right[k * kChannels];
            a1 += kl * left[-k * kChannels + 1] + kr * right[k * kChannels + 1];
        }
        out[j * kChannels] = a0;
        out[j * kChannels + 1] = a1;
        pos_ += ratio;
    }

    // Drop history that can no longer be reached.
    const auto i0 = static_cast<std::size_t>(pos_);
    const std::size_t keep =
        i0 > static_cast<std::size_t>(reach) ? i0 - static_cast<std::size_t>(reach) : 0;
    if (keep > 0) {
        std::memmove(&buf_[0], &buf_[keep * kChannels], (len_ - keep) * kChannels * sizeof(float));
        len_ -= keep;
        pos_ -= static_cast<double>(keep);
    }
    return n;
}

} // namespace lsq
