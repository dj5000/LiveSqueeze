#include "analyze.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lsqcli {
namespace {

constexpr double kPi = 3.14159265358979323846;

double toDb(double lin) {
    return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0;
}

double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 80; ++k) {
        term *= (x * x * 0.25) / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
    }
    return sum;
}

// 16x windowed-sinc reconstruction filters, kHalf samples either side.
struct Bank {
    static constexpr int kOversample = 16;
    static constexpr int kHalf = 32;
    std::vector<double> coef;
    Bank() : coef(static_cast<std::size_t>(kOversample * 2 * kHalf)) {
        const double i0 = besselI0(9.0);
        for (int j = 0; j < kOversample; ++j) {
            const double frac = static_cast<double>(j) / kOversample;
            for (int t = 0; t < 2 * kHalf; ++t) {
                const double x = static_cast<double>(t - kHalf + 1) - frac;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = x / kHalf;
                const double w =
                    std::fabs(r) >= 1.0 ? 0.0 : besselI0(9.0 * std::sqrt(1.0 - r * r)) / i0;
                coef[static_cast<std::size_t>(j * 2 * kHalf + t)] = sinc * w;
            }
        }
    }
};

} // namespace

std::vector<ChannelStats> analyzeChannels(const std::vector<float>& x, int channels) {
    static const Bank bank;
    const auto nch = static_cast<std::size_t>(channels);
    const std::size_t frames = x.size() / nch;
    std::vector<ChannelStats> stats(nch);

    for (std::size_t c = 0; c < nch; ++c) {
        double peak = 0.0;
        double sum = 0.0;
        for (std::size_t f = 0; f < frames; ++f) {
            const double v = static_cast<double>(x[f * nch + c]);
            peak = std::max(peak, std::fabs(v));
            sum += v * v;
        }
        stats[c].samplePeakDb = toDb(peak);
        stats[c].rmsDb = frames > 0
                             ? 10.0 * std::log10(std::max(sum / static_cast<double>(frames), 1e-24))
                             : -240.0;

        // True peak: reconstruct 16 points per sample interval, skipping the file edges.
        double tp = peak;
        const std::size_t half = Bank::kHalf;
        for (std::size_t f = half; f + half < frames; ++f) {
            for (int j = 1; j < Bank::kOversample; ++j) {
                const double* k = &bank.coef[static_cast<std::size_t>(j * 2 * Bank::kHalf)];
                double acc = 0.0;
                for (int t = 0; t < 2 * Bank::kHalf; ++t) {
                    const std::size_t idx = f + static_cast<std::size_t>(t) - half + 1;
                    acc += k[t] * static_cast<double>(x[idx * nch + c]);
                }
                tp = std::max(tp, std::fabs(acc));
            }
        }
        stats[c].truePeakDb = toDb(tp);
    }
    return stats;
}

LevelSpread levelSpread(const std::vector<float>& x, int channels, double sampleRate) {
    const auto nch = static_cast<std::size_t>(channels);
    const std::size_t frames = x.size() / nch;
    const auto win = static_cast<std::size_t>(0.4 * sampleRate);
    std::vector<double> levels;
    for (std::size_t f0 = 0; f0 + win <= frames; f0 += win / 4) {
        double sum = 0.0;
        for (std::size_t f = f0; f < f0 + win; ++f) {
            for (std::size_t c = 0; c < nch; ++c) {
                const double v = static_cast<double>(x[f * nch + c]);
                sum += v * v;
            }
        }
        const double db = 10.0 * std::log10(std::max(sum / static_cast<double>(win * nch), 1e-24));
        if (db > -70.0) { // ignore digital silence and the noise floor
            levels.push_back(db);
        }
    }
    LevelSpread s;
    if (levels.size() < 4) {
        return s;
    }
    std::sort(levels.begin(), levels.end());
    s.p10 = levels[levels.size() / 10];
    s.p95 = levels[levels.size() * 95 / 100];
    return s;
}

} // namespace lsqcli
