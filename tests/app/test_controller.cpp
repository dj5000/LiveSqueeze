#include <QSignalSpy>
#include <cmath>

#include "AppController.hpp"
#include "app_test_util.hpp"
#include "doctest.h"
#include "lsq/presets.hpp"

using namespace lsqapp;
using lsqtest::Fixture;
using lsqtest::waitFor;

namespace {

bool running(AppController& c) {
    return c.state() == lsq::SupervisorState::Running;
}

// Collects meter snapshots for about `ms` and returns the loudest/lowest values seen.
MeterSnapshot collect(AppController& c, int ms) {
    MeterSnapshot total;
    QSignalSpy spy(&c, &AppController::metersUpdated);
    waitFor([&] { return false; }, ms);
    for (const QList<QVariant>& args : spy) {
        const auto m = args.at(0).value<MeterSnapshot>();
        total.any = total.any || m.any;
        total.inPeakDb = std::max(total.inPeakDb, m.inPeakDb);
        total.outPeakDb = std::max(total.outPeakDb, m.outPeakDb);
        total.reductionDb = std::min(total.reductionDb, m.reductionDb);
        total.boostDb = std::max(total.boostDb, m.boostDb);
    }
    return total;
}

} // namespace

TEST_CASE("controller: starts on simulated devices and reports levels") {
    Fixture f;
    AppController c(f.options);
    c.setMetersVisible(true);
    REQUIRE(waitFor([&] { return running(c); }));
    CHECK(c.processingEnabled());
    CHECK_FALSE(c.statusText().isEmpty());

    const MeterSnapshot m = collect(c, 800);
    REQUIRE(m.any);
    CHECK(m.inPeakDb == doctest::Approx(-20.0).epsilon(0.05)); // the fake source: -20 dBFS sine
    CHECK(c.engineStats().running);
}

TEST_CASE("controller: a slider move reaches the audio engine") {
    Fixture f;
    AppController c(f.options);
    c.setMetersVisible(true);
    REQUIRE(waitFor([&] { return running(c); }));

    // Movie / Night squeezes a -20 dB tone (threshold -24 dB, 4:1): the output is lower.
    collect(c, 600);
    const MeterSnapshot on = collect(c, 600);
    CHECK(on.reductionDb < -1.0f);
    CHECK(on.outPeakDb < on.inPeakDb - 1.0f);

    // Bypass: the same tone passes unchanged.
    c.setBypass(true);
    collect(c, 300);
    const MeterSnapshot off = collect(c, 600);
    CHECK(off.reductionDb > -0.1f);
    CHECK(off.outPeakDb == doctest::Approx(static_cast<double>(off.inPeakDb)).epsilon(0.02));

    // A threshold above the tone leaves it alone as well.
    c.setBypass(false);
    c.setParam(QStringLiteral("threshold_db"), -5.0);
    c.setParam(QStringLiteral("upward_enabled"), 0.0);
    collect(c, 1800); // the compressor lets go with a 400 ms time constant
    const MeterSnapshot high = collect(c, 500);
    CHECK(high.reductionDb > -0.5f);
}

TEST_CASE("controller: parameters, presets and the strength macro") {
    Fixture f(false);
    AppController c(f.options);
    QSignalSpy params(&c, &AppController::paramsChanged);
    QSignalSpy presetSpy(&c, &AppController::presetChanged);

    CHECK(c.presetKey() == QStringLiteral("movie-night"));
    CHECK(c.strength() == 50);

    c.setParam(QStringLiteral("threshold_db"), -31.0);
    CHECK(c.paramValue(QStringLiteral("threshold_db")) == doctest::Approx(-31.0));
    CHECK(c.strength() == -1); // no longer a named setting
    CHECK(c.presetKey().isEmpty());
    CHECK(params.count() == 1);
    CHECK(presetSpy.count() == 1);

    c.setParam(QStringLiteral("threshold_db"), -31.0); // no change: no signals
    CHECK(params.count() == 1);

    // Volume and bypass are the user's own and do not make the sound "custom".
    c.setPreset(QStringLiteral("dialogue-boost"));
    CHECK(c.presetKey() == QStringLiteral("dialogue-boost"));
    c.setParam(QStringLiteral("out_trim_db"), -9.0);
    CHECK(c.presetKey() == QStringLiteral("dialogue-boost"));
    c.setBypass(true);
    CHECK(c.presetKey() == QStringLiteral("dialogue-boost"));

    // A preset keeps the volume, the bypass switch and the LFE choice.
    c.setParam(QStringLiteral("lfe_enabled"), 1.0);
    c.setPreset(QStringLiteral("late-night"));
    const lsq::Params p = c.params();
    CHECK(p.outTrimDb == doctest::Approx(-9.0));
    CHECK(p.bypass);
    CHECK(p.lfeEnabled);
    CHECK(p.ratio == lsq::presetParams(lsq::Preset::LateNight).ratio);

    c.setPreset(QStringLiteral("no-such-preset")); // ignored
    CHECK(c.presetKey() == QStringLiteral("late-night"));

    // Strength.
    c.setStrength(0);
    CHECK(c.strength() == 0);
    CHECK_FALSE(c.params().compEnabled);
    CHECK(c.params().outTrimDb == doctest::Approx(-9.0));
    c.setStrength(50);
    CHECK(c.presetKey() == QStringLiteral("movie-night"));
    CHECK(c.params().ratio == lsq::presetParams(lsq::Preset::MovieNight).ratio);
    c.setStrength(100);
    CHECK(c.params().ratio == lsq::presetParams(lsq::Preset::LateNight).ratio);
    c.setStrength(1000); // clamped
    CHECK(c.strength() == 100);
}

TEST_CASE("controller: user presets are saved, applied and deleted") {
    Fixture f(false);
    AppController c(f.options);
    c.setStrength(70);
    c.setParam(QStringLiteral("release_ms"), 777.0);
    CHECK_FALSE(c.saveUserPreset(QStringLiteral("   ")));
    REQUIRE(c.saveUserPreset(QStringLiteral("Night owl")));
    CHECK(c.presetKey() == QStringLiteral("user:Night owl"));
    CHECK(c.userPresets().size() == 1);

    c.setPreset(QStringLiteral("gentle-leveler"));
    CHECK(c.paramValue(QStringLiteral("release_ms")) != doctest::Approx(777.0));
    c.applyUserPreset(QStringLiteral("night owl")); // names are not case sensitive
    CHECK(c.paramValue(QStringLiteral("release_ms")) == doctest::Approx(777.0));
    CHECK(c.presetKey() == QStringLiteral("user:Night owl"));

    // The bypass switch is not saved with a preset.
    c.setBypass(true);
    REQUIRE(c.saveUserPreset(QStringLiteral("Night owl")));
    c.setBypass(false);
    c.applyUserPreset(QStringLiteral("Night owl"));
    CHECK_FALSE(c.bypass());
    CHECK(c.userPresets().size() == 1); // replaced, not duplicated

    CHECK(c.deleteUserPreset(QStringLiteral("Night owl")));
    CHECK(c.userPresets().isEmpty());
    CHECK(c.presetKey().isEmpty());
}

TEST_CASE("controller: settings are remembered between runs") {
    Fixture f(false);
    {
        AppController c(f.options);
        c.setStrength(80);
        c.setParam(QStringLiteral("ratio"), 7.5);
        c.setOutputDevice(QStringLiteral("some-speakers"));
        c.setLatencyMode(2);
        c.setSinkLayout(QStringLiteral("7.1"));
        c.setStartMinimized(true);
        c.setProcessingEnabled(false);
    } // the destructor saves
    AppController d(f.options);
    CHECK(d.paramValue(QStringLiteral("ratio")) == doctest::Approx(7.5));
    CHECK(d.strength() == -1);
    CHECK(d.outputDevice() == QStringLiteral("some-speakers"));
    CHECK(d.latencyMode() == 2);
    CHECK(d.sinkLayout() == QStringLiteral("7.1"));
    CHECK(d.startMinimized());
    CHECK_FALSE(d.processingEnabled());
}

TEST_CASE("controller: stopping, starting and choosing devices") {
    Fixture f;
    AppController c(f.options);
    REQUIRE(waitFor([&] { return running(c); }));
    QSignalSpy states(&c, &AppController::stateChanged);

    c.setProcessingEnabled(false);
    CHECK(c.state() == lsq::SupervisorState::Idle);
    CHECK_FALSE(c.processingEnabled());
    CHECK(states.count() >= 1);
    CHECK(c.engineStats().captureCallbacks == 0);

    c.setProcessingEnabled(true);
    CHECK(waitFor([&] { return running(c); }));

    // An output that does not exist is reported, not hidden; choosing automatic recovers.
    c.setOutputDevice(QStringLiteral("no-such-device"));
    CHECK(waitFor([&] { return c.state() == lsq::SupervisorState::Recovering; }));
    CHECK_FALSE(c.statusText().isEmpty());
    c.setOutputDevice(QString());
    CHECK(waitFor([&] { return running(c); }, 8000));

    c.setLatencyMode(0);
    CHECK(waitFor([&] { return running(c); }, 8000));
    CHECK(c.engineStats().latencyMs < 40.0);
}

TEST_CASE("controller: devices and diagnostics") {
    Fixture f;
    AppController c(f.options);
    REQUIRE(waitFor([&] { return running(c); }));

    const auto outs = c.devices(false);
    const auto ins = c.devices(true);
    REQUIRE_FALSE(outs.isEmpty());
    REQUIRE_FALSE(ins.isEmpty());
    CHECK(outs[0].isDefault);
    CHECK(ins[0].looksVirtual);

    const QString text = c.diagnostics();
    CHECK(text.contains(QStringLiteral("LiveSqueeze ")));
    CHECK(text.contains(QStringLiteral("Audio backend: fake")));
    CHECK(text.contains(QStringLiteral("State: running"), Qt::CaseInsensitive));
    CHECK(text.contains(QStringLiteral("threshold_db")));
    CHECK(text.contains(QStringLiteral("Fake speakers")));
    CHECK(text.contains(QStringLiteral("underruns")));
}
