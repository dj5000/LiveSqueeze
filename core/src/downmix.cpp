#include "lsq/downmix.hpp"

#include <algorithm>

#include "lsq/db.hpp"

namespace lsq {
namespace {
constexpr float kInvSqrt2 = 0.70710678118654752f;
}

void Downmixer::computeCoefficients(const ChannelMap& map, const Params& p, float* cl,
                                    float* cr) noexcept {
    for (int i = 0; i < kMaxChannels; ++i) {
        cl[i] = 0.0f;
        cr[i] = 0.0f;
    }

    const bool hasFront = map.has(Pos::FL) || map.has(Pos::FR);
    const float c = kInvSqrt2 * dbToLin(p.centerGainDb);
    const float s = kInvSqrt2 * dbToLin(p.surroundGainDb);
    const float lfe = p.lfeEnabled ? dbToLin(p.lfeGainDb) : 0.0f;

    bool anyKnown = false;
    for (int i = 0; i < map.n; ++i) {
        switch (map.pos[i]) {
        case Pos::FL:
            cl[i] = 1.0f;
            break;
        case Pos::FR:
            cr[i] = 1.0f;
            break;
        case Pos::FC:
            // A lone center channel is a mono source: copy it at unity.
            cl[i] = cr[i] = hasFront ? c : 1.0f;
            break;
        case Pos::LFE:
            cl[i] = cr[i] = lfe;
            break;
        case Pos::BL:
        case Pos::SL:
            cl[i] = s;
            break;
        case Pos::BR:
        case Pos::SR:
            cr[i] = s;
            break;
        case Pos::FLC:
            cl[i] = kInvSqrt2;
            break;
        case Pos::FRC:
            cr[i] = kInvSqrt2;
            break;
        case Pos::BC:
            cl[i] = cr[i] = s * kInvSqrt2;
            break;
        case Pos::Unknown:
            break;
        }
        anyKnown = anyKnown || map.pos[i] != Pos::Unknown;
    }

    if (!anyKnown && map.n >= 1) {
        // Nothing is labelled: treat the first two channels as left and right.
        cl[0] = 1.0f;
        if (map.n >= 2) {
            cr[1] = 1.0f;
        } else {
            cr[0] = 1.0f;
        }
    }

    if (p.normalizeDownmix) {
        float sumL = 0.0f;
        float sumR = 0.0f;
        for (int i = 0; i < map.n; ++i) {
            sumL += cl[i];
            sumR += cr[i];
        }
        const float m = std::max(sumL, sumR);
        if (m > 1.0f) {
            for (int i = 0; i < map.n; ++i) {
                cl[i] /= m;
                cr[i] /= m;
            }
        }
    }
}

void Downmixer::prepare(const ChannelMap& input, double sampleRate) noexcept {
    map_ = input;
    rampSamples_ = std::max(1, static_cast<int>(sampleRate * 0.030));
    initialized_ = false;
}

void Downmixer::process(const float* in, float* out, std::size_t frames, const Params& p) noexcept {
    const int n = map_.n;
    if (n <= 0) {
        std::fill(out, out + 2 * frames, 0.0f);
        return;
    }

    float tl[kMaxChannels];
    float tr[kMaxChannels];
    computeCoefficients(map_, p, tl, tr);

    if (!initialized_) {
        for (int c = 0; c < n; ++c) {
            left_[c].reset(tl[c]);
            right_[c].reset(tr[c]);
        }
        initialized_ = true;
    }

    bool ramping = false;
    for (int c = 0; c < n; ++c) {
        left_[c].setTarget(tl[c], rampSamples_);
        right_[c].setTarget(tr[c], rampSamples_);
        ramping = ramping || left_[c].active() || right_[c].active();
    }

    if (!ramping) {
        float cl[kMaxChannels];
        float cr[kMaxChannels];
        for (int c = 0; c < n; ++c) {
            cl[c] = left_[c].current();
            cr[c] = right_[c].current();
        }
        for (std::size_t f = 0; f < frames; ++f) {
            const float* x = in + f * static_cast<std::size_t>(n);
            float l = 0.0f;
            float r = 0.0f;
            for (int c = 0; c < n; ++c) {
                l += cl[c] * x[c];
                r += cr[c] * x[c];
            }
            out[2 * f] = l;
            out[2 * f + 1] = r;
        }
        return;
    }

    for (std::size_t f = 0; f < frames; ++f) {
        const float* x = in + f * static_cast<std::size_t>(n);
        float l = 0.0f;
        float r = 0.0f;
        for (int c = 0; c < n; ++c) {
            l += left_[c].next() * x[c];
            r += right_[c].next() * x[c];
        }
        out[2 * f] = l;
        out[2 * f + 1] = r;
    }
}

} // namespace lsq
