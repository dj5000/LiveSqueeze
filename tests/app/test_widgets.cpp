#include <QCloseEvent>
#include <QSignalSpy>
#include <QTest>

#include "MainWindow.hpp"
#include "Widgets.hpp"
#include "app_test_util.hpp"
#include "doctest.h"
#include "lsq/presets.hpp"

using namespace lsqapp;
using lsqtest::Fixture;
using lsqtest::waitFor;

TEST_CASE("param row: a number setting keeps slider and number box together") {
    const lsq::ParamDesc& d = *lsq::findParam("threshold_db"); // -60 .. 0 dB
    ParamRow row(d);
    QSignalSpy spy(&row, &ParamRow::valueEdited);
    row.show();

    row.setValue(-30.0); // programmatic: no signal
    CHECK(spy.count() == 0);
    CHECK(row.spin()->value() == doctest::Approx(-30.0));
    CHECK(row.slider()->value() == 300); // steps of 0.1 dB from -60
    CHECK(row.value() == doctest::Approx(-30.0));

    row.spin()->setValue(-12.5); // as typed
    REQUIRE(spy.count() == 1);
    CHECK(spy.at(0).at(0).toString() == QStringLiteral("threshold_db"));
    CHECK(spy.at(0).at(1).toDouble() == doctest::Approx(-12.5));
    CHECK(row.slider()->value() == 475);

    row.slider()->setValue(100); // dragged: -50 dB
    REQUIRE(spy.count() == 2);
    CHECK(spy.at(1).at(1).toDouble() == doctest::Approx(-50.0));
    CHECK(row.spin()->value() == doctest::Approx(-50.0));

    CHECK(row.spin()->suffix() == QStringLiteral(" dB"));
    CHECK(row.slider()->accessibleName() == QStringLiteral("Threshold"));
    CHECK_FALSE(row.slider()->accessibleDescription().isEmpty());
}

TEST_CASE("param row: on/off settings are check boxes") {
    const lsq::ParamDesc& d = *lsq::findParam("lfe_enabled");
    ParamRow row(d);
    QSignalSpy spy(&row, &ParamRow::valueEdited);
    REQUIRE(row.check() != nullptr);
    CHECK(row.slider() == nullptr);
    row.setValue(1.0);
    CHECK(spy.count() == 0);
    CHECK(row.check()->isChecked());
    row.check()->click();
    REQUIRE(spy.count() == 1);
    CHECK(spy.at(0).at(1).toDouble() == 0.0);
}

TEST_CASE("param row: every parameter has a working control and a help text") {
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        ParamRow row(d);
        CHECK_MESSAGE(!paramHelp(QString::fromLatin1(d.key)).isEmpty(), d.key);
        row.setValue(static_cast<double>(d.defaultValue));
        CHECK_MESSAGE(row.value() ==
                          doctest::Approx(static_cast<double>(d.defaultValue)).epsilon(0.01),
                      d.key);
        row.setValue(static_cast<double>(d.maxValue));
        if (d.kind == lsq::ParamKind::Float) {
            CHECK_MESSAGE(row.value() == doctest::Approx(static_cast<double>(d.maxValue)), d.key);
        }
    }
}

TEST_CASE("level meter: shows a number, and only announces it about once a second") {
    LevelMeter meter(QStringLiteral("Input level"));
    CHECK(meter.accessibleName() == QStringLiteral("Input level"));
    const QString before = meter.accessibleDescription();
    for (int i = 0; i < 200; ++i) {
        meter.setLevel(-30.0f + 0.1f * static_cast<float>(i)); // a fast stream of readings
    }
    CHECK(meter.accessibleDescription() == before); // a screen reader hears nothing yet
    meter.announce();
    CHECK(meter.valueText() == QStringLiteral("-10.1 dB"));
    CHECK(meter.accessibleDescription() == QStringLiteral("-10.1 dB"));
    // The bar jumps up at once and falls back slowly.
    CHECK(meter.displayedDb() > -11.0f);
    // Silence reads as a word, not as -120 dB.
    LevelMeter quiet(QStringLiteral("Q"));
    quiet.announce();
    CHECK(quiet.valueText() == QStringLiteral("silent"));
}

TEST_CASE("gain meter: says in words how far things were turned down and lifted") {
    GainMeter meter(QStringLiteral("Gain change"));
    meter.setGain(-6.2f, 3.1f);
    meter.setGain(-4.0f, 1.0f);
    meter.announce();
    CHECK(meter.valueText() == QStringLiteral("turned down 6.2 dB, lifted 3.1 dB"));
    CHECK(meter.accessibleDescription() == meter.valueText());
}

TEST_CASE("curve view: describes the curve in words") {
    CurveView view;
    lsq::Params p = lsq::presetParams(lsq::Preset::MovieNight);
    view.setParams(p);
    CHECK(view.summary().contains(QStringLiteral("-24")));
    CHECK(view.summary().contains(QStringLiteral("4.0 to 1")));
    CHECK(view.summary().contains(QStringLiteral("-34")));
    CHECK(view.accessibleDescription() == view.summary());
    p.compEnabled = false;
    view.setParams(p);
    CHECK(view.summary().contains(QStringLiteral("compressor is off")));
    p.bypass = true;
    view.setParams(p);
    CHECK(view.summary().contains(QStringLiteral("Bypass is on")));
    CHECK(view.heightForWidth(300) == 300);
}

TEST_CASE("main window: controls and controller stay in step") {
    Fixture f;
    AppController c(f.options);
    MainWindow w(&c);
    w.show();
    REQUIRE(waitFor([&] { return c.state() == lsq::SupervisorState::Running; }));
    CHECK(waitFor([&] { return w.statusLabel()->text().contains(QStringLiteral("Running")); }));

    // Window -> controller
    w.row(QStringLiteral("out_trim_db"))->spin()->setValue(-15.0);
    CHECK(c.paramValue(QStringLiteral("out_trim_db")) == doctest::Approx(-15.0));
    w.advancedRow(QStringLiteral("threshold_db"))->spin()->setValue(-40.0);
    CHECK(c.paramValue(QStringLiteral("threshold_db")) == doctest::Approx(-40.0));
    CHECK(w.strengthSlider()->value() == 50); // unchanged position, but now "custom"
    CHECK(c.strength() == -1);
    CHECK(w.presetCombo()->currentText() == QStringLiteral("Custom"));

    // Controller -> window (both copies of a shared setting)
    c.setParam(QStringLiteral("center_gain_db"), 6.0);
    CHECK(w.row(QStringLiteral("center_gain_db"))->value() == doctest::Approx(6.0));
    CHECK(w.advancedRow(QStringLiteral("center_gain_db"))->value() == doctest::Approx(6.0));

    // The strength slider
    w.strengthSlider()->setValue(80);
    CHECK(c.strength() == 80);
    CHECK(w.advancedRow(QStringLiteral("ratio"))->value() ==
          doctest::Approx(static_cast<double>(lsq::strengthParams(80.0f).ratio)).epsilon(0.01));
    CHECK(c.paramValue(QStringLiteral("out_trim_db")) == doctest::Approx(-15.0)); // kept

    // Presets
    const int idx = w.presetCombo()->findData(QStringLiteral("late-night"));
    REQUIRE(idx >= 0);
    QTest::keyClick(w.presetCombo(),
                    Qt::Key_Down); // moves the selection; activated() is what applies it
    w.presetCombo()->setCurrentIndex(idx);
    emit w.presetCombo()->activated(idx);
    CHECK(c.presetKey() == QStringLiteral("late-night"));
    CHECK(w.presetCombo()->currentText() == QStringLiteral("Late night"));

    // Bypass, both ways
    w.bypassCheck()->click();
    CHECK(c.bypass());
    CHECK(w.statusLabel()->text().contains(QStringLiteral("Bypassed")));
    c.setBypass(false);
    CHECK_FALSE(w.bypassCheck()->isChecked());

    // Stop and start
    w.processingButton()->click();
    CHECK_FALSE(c.processingEnabled());
    CHECK(w.statusLabel()->text().contains(QStringLiteral("Stopped")));
    w.processingButton()->click();
    CHECK(c.processingEnabled());
    CHECK(
        waitFor([&] { return w.statusLabel()->text().contains(QStringLiteral("Running")); }, 8000));
}

TEST_CASE("main window: meters follow the audio") {
    Fixture f;
    AppController c(f.options);
    MainWindow w(&c);
    w.show(); // visible: meters poll fast
    REQUIRE(waitFor([&] { return c.state() == lsq::SupervisorState::Running; }));
    CHECK(waitFor([&] { return w.inputMeter()->displayedDb() > -30.0f; }));
    w.inputMeter()->announce();
    CHECK(w.inputMeter()->valueText().contains(QStringLiteral("-2")));
    CHECK(w.curve()->accessibleDescription().contains(QStringLiteral("limiter")));
}

TEST_CASE("main window: closing hides it when there is a tray, quits when there is not") {
    Fixture f(false);
    AppController c(f.options);
    {
        MainWindow w(&c);
        QSignalSpy quit(&w, &MainWindow::quitRequested);
        w.setTrayAvailable(true);
        w.show();
        w.close();
        CHECK_FALSE(w.isVisible());
        CHECK(quit.count() == 0);
    }
    {
        MainWindow w(&c);
        QSignalSpy quit(&w, &MainWindow::quitRequested);
        w.setTrayAvailable(false);
        w.show();
        w.close();
        CHECK(quit.count() == 1);
    }
}
