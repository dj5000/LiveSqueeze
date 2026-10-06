#include <cmath>
#include <set>
#include <string>

#include "doctest.h"
#include "lsq/gain_computer.hpp"
#include "lsq/param_store.hpp"
#include "lsq/params.hpp"
#include "lsq/presets.hpp"
#include "test_util.hpp"

using namespace lsq;

TEST_CASE("parameter table is consistent") {
    std::set<std::string> keys;
    const Params defaults;
    CHECK(paramCount() >= 20);
    for (std::size_t i = 0; i < paramCount(); ++i) {
        const ParamDesc& d = paramDesc(i);
        CHECK(keys.insert(d.key).second); // unique
        CHECK(d.minValue <= d.defaultValue);
        CHECK(d.defaultValue <= d.maxValue);
        CHECK(paramGet(defaults, d) == d.defaultValue); // table agrees with the struct
        CHECK(findParam(d.key) == &d);
    }
    CHECK(findParam("no_such_parameter") == nullptr);
}

TEST_CASE("paramSet clamps and handles NaN") {
    Params p;
    const ParamDesc* d = findParam("ratio");
    REQUIRE(d != nullptr);
    paramSet(p, *d, 1000.0f);
    CHECK(p.ratio == d->maxValue);
    paramSet(p, *d, -5.0f);
    CHECK(p.ratio == d->minValue);
    paramSet(p, *d, std::nanf(""));
    CHECK(p.ratio == d->defaultValue);

    const ParamDesc* b = findParam("bypass");
    REQUIRE(b != nullptr);
    paramSet(p, *b, 1.0f);
    CHECK(p.bypass);
    paramSet(p, *b, 0.0f);
    CHECK_FALSE(p.bypass);
}

TEST_CASE("sanitize repairs out-of-range and inconsistent values") {
    Params p;
    p.thresholdDb = -40.0f;
    p.upThresholdDb = -10.0f; // boost threshold above compression threshold: not allowed
    p.attackMs = -3.0f;
    p.ceilingDb = 6.0f;
    p.outTrimDb = std::nanf("");
    sanitize(p);
    CHECK(p.upThresholdDb <= p.thresholdDb);
    CHECK(p.attackMs >= 0.1f);
    CHECK(p.ceilingDb <= 0.0f);
    CHECK(std::isfinite(p.outTrimDb));
}

TEST_CASE("applyAssignment parses key=value") {
    Params p;
    std::string err;
    CHECK(applyAssignment(p, "threshold_db=-30", &err));
    CHECK(p.thresholdDb == -30.0f);
    CHECK(applyAssignment(p, "lfe_enabled=on", &err));
    CHECK(p.lfeEnabled);
    CHECK(applyAssignment(p, "upward_enabled=false", &err));
    CHECK_FALSE(p.upwardEnabled);

    CHECK_FALSE(applyAssignment(p, "threshold_db", &err));
    CHECK_FALSE(applyAssignment(p, "bogus=1", &err));
    CHECK(err.find("bogus") != std::string::npos);
    CHECK_FALSE(applyAssignment(p, "ratio=abc", &err));
    CHECK_FALSE(applyAssignment(p, "bypass=maybe", &err));

    // Out-of-range numbers are clamped, and Tu is kept at or below Td.
    CHECK(applyAssignment(p, "threshold_db=-60", &err));
    CHECK(p.upThresholdDb <= p.thresholdDb);
}

TEST_CASE("presets are valid and distinct") {
    CHECK(presetParams(Preset::MovieNight).thresholdDb == Params{}.thresholdDb);
    std::set<std::string> keys;
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const auto preset = static_cast<Preset>(i);
        Params p = presetParams(preset);
        Params q = p;
        sanitize(q);
        for (std::size_t k = 0; k < paramCount(); ++k) {
            CHECK(paramGet(p, paramDesc(k)) == paramGet(q, paramDesc(k))); // already sanitized
        }
        CHECK(keys.insert(presetKey(preset)).second);
        Preset back;
        CHECK(presetFromKey(presetKey(preset), back));
        CHECK(back == preset);
        CHECK(std::string(presetName(preset)).size() > 3);
    }
    Preset dummy;
    CHECK_FALSE(presetFromKey("nope", dummy));
    CHECK_FALSE(presetParams(Preset::LimiterOnly).compEnabled);
    CHECK_FALSE(presetParams(Preset::GentleLeveler).upwardEnabled);
    CHECK(presetParams(Preset::DialogueBoost).centerGainDb >
          presetParams(Preset::MovieNight).centerGainDb);
}

TEST_CASE("blendParams interpolates") {
    const Params a = presetParams(Preset::GentleLeveler);
    const Params b = presetParams(Preset::LateNight);
    const Params m0 = blendParams(a, b, 0.0f);
    const Params m1 = blendParams(a, b, 1.0f);
    const Params mh = blendParams(a, b, 0.5f);
    CHECK(m0.thresholdDb == a.thresholdDb);
    CHECK(m1.thresholdDb == b.thresholdDb);
    CHECK_NEAR(mh.thresholdDb, 0.5f * (a.thresholdDb + b.thresholdDb), 1e-4);
    CHECK(m0.upwardEnabled == a.upwardEnabled);
    CHECK(m1.upwardEnabled == b.upwardEnabled);
}

TEST_CASE("ParamStore hands over the latest value") {
    ParamStore store;
    Params out;
    CHECK_FALSE(store.fetch(out)); // nothing published yet

    Params p;
    p.thresholdDb = -33.0f;
    store.publish(p);
    CHECK(store.fetch(out));
    CHECK(out.thresholdDb == -33.0f);
    CHECK_FALSE(store.fetch(out)); // already consumed

    p.thresholdDb = -10.0f;
    store.publish(p);
    p.thresholdDb = -20.0f;
    store.publish(p); // the reader only needs the newest
    CHECK(store.fetch(out));
    CHECK(out.thresholdDb == -20.0f);

    p.ratio = 999.0f; // publish sanitizes
    store.publish(p);
    CHECK(store.fetch(out));
    CHECK(out.ratio <= 20.0f);
}

TEST_CASE("the strength macro runs from limiter-only through Movie / Night to Late night") {
    const Params off = strengthParams(0.0f);
    const Params mid = strengthParams(50.0f);
    const Params max = strengthParams(100.0f);
    const Params movie = presetParams(Preset::MovieNight);
    const Params late = presetParams(Preset::LateNight);

    // The ends are the presets of the same name, and the middle is Movie / Night.
    for (std::size_t i = 0; i < paramCount(); ++i) {
        const ParamDesc& d = paramDesc(i);
        CHECK(paramGet(mid, d) == paramGet(movie, d));
        CHECK_NEAR(paramGet(max, d), paramGet(late, d), 1e-4);
    }
    for (float level = -80.0f; level <= 0.0f; level += 2.0f) {
        CHECK(curve::staticGainDb(level, off) == 0.0f); // nothing but the limiter
    }

    // More strength squeezes loud sounds more and lifts quiet ones more, step by step.
    float loudBefore = 1.0f;
    float quietBefore = -1.0f;
    for (int percent = 0; percent <= 100; ++percent) {
        const Params p = strengthParams(static_cast<float>(percent));
        const float loud = curve::staticGainDb(-10.0f, p);
        const float quiet = curve::staticGainDb(-45.0f, p);
        CHECK(loud <= loudBefore + 1e-4f);
        CHECK(quiet >= quietBefore - 1e-4f);
        loudBefore = loud;
        quietBefore = quiet;
    }

    // It never touches the user's own settings.
    CHECK(strengthParams(30.0f).outTrimDb == Params{}.outTrimDb);
    CHECK_FALSE(strengthParams(30.0f).bypass);
    CHECK(strengthParams(-5.0f).compEnabled == off.compEnabled); // clamped
    CHECK(strengthParams(500.0f).ratio == max.ratio);
}
