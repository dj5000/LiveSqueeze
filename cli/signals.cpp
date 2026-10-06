#include "signals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lsqcli {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rng {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    double next() { // [-1, 1)
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<double>(s >> 11) / 4503599627370496.0 - 1.0;
    }
};

double dbToLin(double db) {
    return std::pow(10.0, db / 20.0);
}

// Speech-like: a voiced source (harmonic series, wandering pitch) shaped by syllable envelopes
// with pauses. Not intelligible, but it has the right level changes for the compressor.
struct Voice {
    double phase = 0.0;
    double pitch = 140.0;
    double env = 0.0;
    double sample(double t, double sr, Rng& rng) {
        const double syllable = 0.5 + 0.5 * std::sin(2.0 * kPi * 4.0 * t);
        const double phrase = std::fmod(t, 2.5) < 1.7 ? 1.0 : 0.0; // pauses between phrases
        const double target = syllable * syllable * phrase;
        env += (target - env) * 0.002;
        pitch = 140.0 + 25.0 * std::sin(2.0 * kPi * 0.7 * t);
        phase += 2.0 * kPi * pitch / sr;
        double v = 0.0;
        for (int h = 1; h <= 12; ++h) {
            v += std::sin(h * phase) / static_cast<double>(h);
        }
        return env * (0.45 * v + 0.05 * rng.next());
    }
};

} // namespace

bool generateSignal(const std::string& kind, const lsq::ChannelMap& map, std::uint32_t sampleRate,
                    double seconds, Audio& out, std::string& error) {
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    const int n = map.n;
    out.sampleRate = sampleRate;
    out.map = map;
    out.samples.assign(frames * static_cast<std::size_t>(n), 0.0f);
    const double sr = static_cast<double>(sampleRate);
    Rng rng;

    auto at = [&](std::size_t f, int c) -> float& {
        return out.samples[f * static_cast<std::size_t>(n) + static_cast<std::size_t>(c)];
    };

    if (kind == "sine") {
        const double amp = dbToLin(-12.0);
        for (std::size_t f = 0; f < frames; ++f) {
            const auto v = static_cast<float>(
                amp * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(f) / sr));
            for (int c = 0; c < n; ++c) {
                at(f, c) = v;
            }
        }
    } else if (kind == "noise") {
        const double amp = dbToLin(-20.0);
        for (std::size_t f = 0; f < frames; ++f) {
            for (int c = 0; c < n; ++c) {
                at(f, c) = static_cast<float>(amp * rng.next());
            }
        }
    } else if (kind == "tones") {
        for (std::size_t f = 0; f < frames; ++f) {
            for (int c = 0; c < n; ++c) {
                const double freq = 300.0 * std::pow(1.5, c);
                at(f, c) = static_cast<float>(
                    dbToLin(-12.0) * std::sin(2.0 * kPi * freq * static_cast<double>(f) / sr));
            }
        }
    } else if (kind == "movie") {
        // Levels are RMS in dBFS per channel: dialogue -30, whispers -45, explosions -12
        // (low-passed noise, as real explosions are mostly low and mid frequencies), room tone far
        // below.
        Voice voice;
        constexpr double kVoiceRms = 0.4; // RMS of Voice::sample() at full envelope
        double ambLp[lsq::kMaxChannels] = {};
        double boomLp[lsq::kMaxChannels] = {};
        for (std::size_t f = 0; f < frames; ++f) {
            const double t = static_cast<double>(f) / sr;

            const bool whisper = std::fmod(t, 10.0) >= 6.0;
            const double dialogue =
                voice.sample(t, sr, rng) * dbToLin(whisper ? -45.0 : -30.0) / kVoiceRms;

            // Explosions: fast attack, slow decay, every 7 s.
            const double tb = std::fmod(t, 7.0);
            const double boom =
                tb < 1.2 ? std::exp(-tb * 4.5) * (tb < 0.005 ? tb / 0.005 : 1.0) : 0.0;

            for (int c = 0; c < n; ++c) {
                const lsq::Pos pos = map.pos[c];
                double v = 0.0;
                const bool isCenter = pos == lsq::Pos::FC;
                const bool isLfe = pos == lsq::Pos::LFE;
                const bool isSurround = pos == lsq::Pos::BL || pos == lsq::Pos::BR ||
                                        pos == lsq::Pos::SL || pos == lsq::Pos::SR;
                if (isCenter || (map.n <= 2 && !isLfe)) {
                    v += dialogue; // stereo and mono sources carry dialogue in the phantom center
                }
                if (isLfe) {
                    v += boom * dbToLin(-6.0) * std::sin(2.0 * kPi * 45.0 * t);
                } else {
                    boomLp[c] +=
                        (rng.next() - boomLp[c]) * 0.3; // RMS 0.2425 for unit uniform noise
                    v += boom * boomLp[c] * dbToLin(-12.0) / 0.2425;
                }
                if (isSurround || map.n <= 2) {
                    ambLp[c] +=
                        (rng.next() - ambLp[c]) * 0.02; // low-passed room tone, about -71 dBFS
                    v += ambLp[c] * dbToLin(-46.0);
                }
                at(f, c) = static_cast<float>(v);
            }
        }
    } else {
        error = "unknown signal kind '" + kind + "' (use sine, noise, tones or movie)";
        return false;
    }
    return true;
}

} // namespace lsqcli
