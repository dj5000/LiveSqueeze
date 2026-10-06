#include <cmath>
#include <utility>
#include <vector>

#include "doctest.h"
#include "lsq/downmix.hpp"
#include "test_util.hpp"

using namespace lsq;

namespace {

constexpr double kInvSqrt2 = 0.70710678118654752;

// What the downmix should do with a unit impulse in a channel at `pos`.
std::pair<double, double> expected(Pos pos, bool hasFront, const Params& p) {
    const double c = kInvSqrt2 * std::pow(10.0, static_cast<double>(p.centerGainDb) / 20.0);
    const double s = kInvSqrt2 * std::pow(10.0, static_cast<double>(p.surroundGainDb) / 20.0);
    const double lfe = p.lfeEnabled ? std::pow(10.0, static_cast<double>(p.lfeGainDb) / 20.0) : 0.0;
    switch (pos) {
    case Pos::FL:
        return {1.0, 0.0};
    case Pos::FR:
        return {0.0, 1.0};
    case Pos::FC:
        return hasFront ? std::pair{c, c} : std::pair{1.0, 1.0};
    case Pos::LFE:
        return {lfe, lfe};
    case Pos::BL:
    case Pos::SL:
        return {s, 0.0};
    case Pos::BR:
    case Pos::SR:
        return {0.0, s};
    case Pos::FLC:
        return {kInvSqrt2, 0.0};
    case Pos::FRC:
        return {0.0, kInvSqrt2};
    case Pos::BC:
        return {s * kInvSqrt2, s * kInvSqrt2};
    case Pos::Unknown:
        return {0.0, 0.0};
    }
    return {0.0, 0.0};
}

// Response of the downmixer to an impulse in each input channel.
std::vector<std::pair<float, float>> impulseMatrix(const ChannelMap& map, const Params& p) {
    std::vector<std::pair<float, float>> result;
    for (int ch = 0; ch < map.n; ++ch) {
        Downmixer d;
        d.prepare(map, 48000.0);
        std::vector<float> in(4 * static_cast<std::size_t>(map.n), 0.0f);
        in[static_cast<std::size_t>(ch)] = 1.0f;
        std::vector<float> out(8, -1.0f);
        d.process(in.data(), out.data(), 4, p);
        result.emplace_back(out[0], out[1]);
        CHECK(out[2] == 0.0f); // later frames are silent
        CHECK(out[3] == 0.0f);
    }
    return result;
}

} // namespace

TEST_CASE("impulse matrix matches the ITU coefficients for every layout") {
    Params p;
    p.centerGainDb = 0.0f;
    p.surroundGainDb = 0.0f;
    for (Layout layout : {Layout::Mono, Layout::Stereo, Layout::Surround21, Layout::Quad,
                          Layout::Surround51, Layout::Surround71}) {
        const ChannelMap map = ChannelMap::standard(layout);
        const bool hasFront = map.has(Pos::FL) || map.has(Pos::FR);
        const auto m = impulseMatrix(map, p);
        for (int ch = 0; ch < map.n; ++ch) {
            const auto e = expected(map.pos[ch], hasFront, p);
            INFO("layout " << map.toString() << ", channel " << ch);
            CHECK_NEAR(m[static_cast<std::size_t>(ch)].first, e.first, 1e-6);
            CHECK_NEAR(m[static_cast<std::size_t>(ch)].second, e.second, 1e-6);
        }
    }
}

TEST_CASE("5.1 defaults: center is boosted to unity, LFE is off") {
    const Params p; // centerGainDb = +3, surroundGainDb = 0, LFE off
    const auto m = impulseMatrix(ChannelMap::standard(Layout::Surround51), p);
    CHECK_NEAR(m[2].first, 0.998816, 1e-4); // 0.7071 * 10^(3/20)
    CHECK_NEAR(m[2].second, 0.998816, 1e-4);
    CHECK(m[3].first == 0.0f);
    CHECK(m[3].second == 0.0f);
    CHECK_NEAR(m[4].first, 0.7071, 1e-4);
}

TEST_CASE("LFE can be switched on") {
    Params p;
    p.lfeEnabled = true;
    p.lfeGainDb = -6.0f;
    const auto m = impulseMatrix(ChannelMap::standard(Layout::Surround51), p);
    CHECK_NEAR(m[3].first, 0.501187, 1e-4);
    CHECK_NEAR(m[3].second, 0.501187, 1e-4);
}

TEST_CASE("a mono source is copied to both sides at unity, ignoring the center boost") {
    Params p;
    p.centerGainDb = 6.0f;
    const auto m = impulseMatrix(ChannelMap::standard(Layout::Mono), p);
    CHECK_NEAR(m[0].first, 1.0, 1e-6);
    CHECK_NEAR(m[0].second, 1.0, 1e-6);
}

TEST_CASE("stereo passes through unchanged whatever the surround settings") {
    Params p;
    p.centerGainDb = 6.0f;
    p.surroundGainDb = -9.0f;
    const auto m = impulseMatrix(ChannelMap::standard(Layout::Stereo), p);
    CHECK(m[0].first == 1.0f);
    CHECK(m[0].second == 0.0f);
    CHECK(m[1].first == 0.0f);
    CHECK(m[1].second == 1.0f);
}

TEST_CASE("unlabelled channels fall back to first two as left and right") {
    ChannelMap map;
    map.n = 3;
    map.pos[0] = map.pos[1] = map.pos[2] = Pos::Unknown;
    const auto m = impulseMatrix(map, Params{});
    CHECK(m[0].first == 1.0f);
    CHECK(m[0].second == 0.0f);
    CHECK(m[1].first == 0.0f);
    CHECK(m[1].second == 1.0f);
    CHECK(m[2].first == 0.0f);
    CHECK(m[2].second == 0.0f);
}

TEST_CASE("normalize keeps fully correlated 7.1 content within full scale") {
    Params p;
    p.normalizeDownmix = true;
    p.lfeEnabled = true;
    const ChannelMap map = ChannelMap::standard(Layout::Surround71);
    float cl[kMaxChannels];
    float cr[kMaxChannels];
    Downmixer::computeCoefficients(map, p, cl, cr);
    double sumL = 0.0;
    double sumR = 0.0;
    for (int i = 0; i < map.n; ++i) {
        sumL += static_cast<double>(cl[i]);
        sumR += static_cast<double>(cr[i]);
    }
    CHECK_NEAR(std::max(sumL, sumR), 1.0, 1e-5);

    p.normalizeDownmix = false;
    Downmixer::computeCoefficients(map, p, cl, cr);
    sumL = 0.0;
    for (int i = 0; i < map.n; ++i) {
        sumL += static_cast<double>(cl[i]);
    }
    CHECK(sumL > 2.0);
}

TEST_CASE("coefficient changes are ramped without clicks") {
    const ChannelMap map = ChannelMap::standard(Layout::Surround51);
    Downmixer d;
    d.prepare(map, 48000.0);

    Params a;
    a.centerGainDb = 0.0f;
    Params b = a;
    b.centerGainDb = -12.0f;

    // Constant center channel at 1.0, nothing else.
    const std::size_t frames = 4000;
    std::vector<float> in(frames * 6, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        in[f * 6 + 2] = 1.0f;
    }
    std::vector<float> out(frames * 2);
    d.process(in.data(), out.data(), 1000, a);
    d.process(in.data() + 1000 * 6, out.data() + 2000, 3000, b);

    CHECK_NEAR(out[2 * 999], 0.70711, 1e-4);
    CHECK_NEAR(out[2 * 3999], 0.70711 * std::pow(10.0, -12.0 / 20.0), 1e-4);

    double maxStep = 0.0;
    for (std::size_t f = 1; f < frames; ++f) {
        maxStep = std::max(maxStep, static_cast<double>(std::fabs(out[2 * f] - out[2 * f - 2])));
        CHECK(out[2 * f] <= out[2 * f - 2] + 1e-7f); // monotonically falling
    }
    CHECK(maxStep < 0.001); // 0.53 total change spread over 1440 samples
}

TEST_CASE("output does not depend on block size, even while ramping") {
    const ChannelMap map = ChannelMap::standard(Layout::Surround51);
    const std::size_t frames = 3000;
    std::vector<float> in(frames * 6);
    lsqtest::Rng rng;
    for (float& x : in) {
        x = 0.3f * rng.next();
    }
    Params a;
    Params b;
    b.centerGainDb = -9.0f;
    b.surroundGainDb = 4.0f;

    auto run = [&](std::size_t block) {
        Downmixer d;
        d.prepare(map, 48000.0);
        std::vector<float> out(frames * 2);
        for (std::size_t f = 0; f < frames; f += block) {
            const std::size_t n = std::min(block, frames - f);
            // Parameters change at frame 1000, a multiple of every block size used here.
            d.process(in.data() + f * 6, out.data() + f * 2, n, f < 1000 ? a : b);
        }
        return out;
    };
    const auto ref = run(frames > 1000 ? 1000 : frames);
    for (std::size_t block : {1u, 8u, 100u, 500u}) {
        CHECK(run(block) == ref);
    }
}
