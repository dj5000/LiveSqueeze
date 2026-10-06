#include <QFile>
#include <QSignalSpy>
#include <QTextStream>

#include "Autostart.hpp"
#include "PresetStore.hpp"
#include "Settings.hpp"
#include "SingleInstance.hpp"
#include "app_test_util.hpp"
#include "doctest.h"
#include "lsq/presets.hpp"

using namespace lsqapp;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

bool sameParams(const lsq::Params& a, const lsq::Params& b) {
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        if (std::fabs(lsq::paramGet(a, d) - lsq::paramGet(b, d)) > 1e-4f) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("settings: a first run starts from the Movie / Night defaults") {
    QTemporaryDir dir;
    Settings s(dir.filePath(QStringLiteral("none.ini")));
    CHECK(sameParams(s.params(), lsq::presetParams(lsq::Preset::MovieNight)));
    CHECK(s.strength() == 50);
    CHECK(s.preset() == QStringLiteral("movie-night"));
    CHECK(s.inputDevice().isEmpty());
    CHECK(s.outputDevice().isEmpty());
    CHECK(s.latencyMode() == 1);
    CHECK(s.sinkLayout() == QStringLiteral("5.1"));
    CHECK(s.processingEnabled());
    CHECK_FALSE(s.startMinimized());
}

TEST_CASE("settings: everything survives a restart") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("s.ini"));
    lsq::Params p = lsq::presetParams(lsq::Preset::LateNight);
    p.outTrimDb = -7.5f;
    p.lfeEnabled = true;
    {
        Settings s(path);
        s.setParams(p);
        s.setStrength(80);
        s.setPreset(QStringLiteral("late-night"));
        s.setInputDevice(QStringLiteral("cable-1"));
        s.setOutputDevice(QStringLiteral("speakers-2"));
        s.setLatencyMode(2);
        s.setInputLayout(QStringLiteral("7.1"));
        s.setSinkLayout(QStringLiteral("7.1"));
        s.setProcessingEnabled(false);
        s.setStartMinimized(true);
        s.setWindowGeometry(QByteArray("geometry"));
        s.sync();
    }
    Settings s(path);
    CHECK(sameParams(s.params(), p));
    CHECK(s.strength() == 80);
    CHECK(s.preset() == QStringLiteral("late-night"));
    CHECK(s.inputDevice() == QStringLiteral("cable-1"));
    CHECK(s.outputDevice() == QStringLiteral("speakers-2"));
    CHECK(s.latencyMode() == 2);
    CHECK(s.inputLayout() == QStringLiteral("7.1"));
    CHECK(s.sinkLayout() == QStringLiteral("7.1"));
    CHECK_FALSE(s.processingEnabled());
    CHECK(s.startMinimized());
    CHECK(s.windowGeometry() == QByteArray("geometry"));
}

TEST_CASE("settings: a damaged file falls back to defaults instead of failing") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.ini"));
    writeFile(path, "this is not an ini file\n[[[params\nthreshold_db=banana\nratio=1e99\n"
                    "[ui]\nstrength=999\n[devices]\nlatency=77\n\xff\xfe\x00garbage");
    Settings s(path);
    const lsq::Params p = s.params();
    const lsq::Params defaults = lsq::presetParams(lsq::Preset::MovieNight);
    CHECK(p.thresholdDb == defaults.thresholdDb); // "banana" is ignored
    CHECK(s.strength() == 50);                    // out of range
    CHECK(s.latencyMode() == 1);                  // out of range
    // And values that parse but are out of range are clamped.
    writeFile(path, "[params]\nratio=1e99\nthreshold_db=-500\nattack_ms=nan\n");
    Settings t(path);
    const lsq::Params q = t.params();
    CHECK(q.ratio <= 20.0f);
    CHECK(q.thresholdDb >= -60.0f);
    CHECK(std::isfinite(q.attackMs));
}

TEST_CASE("preset store: save, replace, delete, reload") {
    QTemporaryDir dir;
    PresetStore store(dir.filePath(QStringLiteral("sub/presets.json")));
    CHECK(store.load().empty());

    UserPreset a;
    a.name = QStringLiteral("Quiet flat");
    a.params = lsq::presetParams(lsq::Preset::LateNight);
    a.params.outTrimDb = -12.0f;
    REQUIRE(store.save(a));
    UserPreset b;
    b.name = QStringLiteral("Dialogue");
    b.params = lsq::presetParams(lsq::Preset::DialogueBoost);
    REQUIRE(store.save(b));

    auto loaded = store.load();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].name == a.name);
    CHECK(sameParams(loaded[0].params, a.params));
    CHECK(sameParams(loaded[1].params, b.params));

    // The same name (in any case) replaces.
    a.name = QStringLiteral("QUIET FLAT");
    a.params.ratio = 3.0f;
    REQUIRE(store.save(a));
    loaded = store.load();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].params.ratio == 3.0f);

    CHECK(store.remove(QStringLiteral("dialogue")));
    CHECK_FALSE(store.remove(QStringLiteral("dialogue")));
    CHECK(store.load().size() == 1);
}

TEST_CASE("preset store: a damaged or unknown file yields no presets and is not trusted") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    PresetStore store(path);

    writeFile(path, "{ not json");
    CHECK(store.load().empty());
    writeFile(path, "[1, 2, 3]");
    CHECK(store.load().empty());
    writeFile(path, R"({"version": 99, "presets": [{"name": "x", "params": {}}]})");
    CHECK(store.load().empty()); // a newer format: do not guess

    // Unknown keys are ignored, wrong types skipped, values clamped, nameless entries dropped.
    writeFile(path, R"({"version": 1, "presets": [
        {"name": "ok", "params": {"ratio": 500, "no_such_key": 1, "threshold_db": "loud",
                                  "bypass": true}},
        {"name": "  ", "params": {}},
        "garbage"]})");
    const auto loaded = store.load();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].name == QStringLiteral("ok"));
    CHECK(loaded[0].params.ratio <= 20.0f);
    CHECK(loaded[0].params.thresholdDb == lsq::Params{}.thresholdDb);
}

#if defined(Q_OS_LINUX)
TEST_CASE("autostart: a desktop entry is created and removed") {
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toLocal8Bit());
    CHECK_FALSE(autostart::isEnabled());
    REQUIRE(autostart::setEnabled(true, QStringLiteral("/opt/live squeeze/livesqueeze")));
    CHECK(autostart::isEnabled());
    QFile f(autostart::entryLocation());
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    CHECK(text.contains(QStringLiteral("Exec=\"/opt/live squeeze/livesqueeze\" --minimized")));
    CHECK(text.contains(QStringLiteral("[Desktop Entry]")));
    f.close();
    REQUIRE(autostart::setEnabled(false, QString()));
    CHECK_FALSE(autostart::isEnabled());
    CHECK(autostart::setEnabled(false, QString())); // already off is fine
    qputenv("XDG_CONFIG_HOME", "/nonexistent-lsq-test-config");
}
#endif

TEST_CASE("single instance: the second copy asks the first to show itself") {
    const QString key =
        QStringLiteral("lsq-test-instance-%1").arg(QCoreApplication::applicationPid());
    SingleInstance first(key);
    REQUIRE(first.isPrimary());
    QSignalSpy spy(&first, &SingleInstance::showRequested);

    SingleInstance second(key);
    CHECK_FALSE(second.isPrimary());
    CHECK(second.notifyPrimary());
    CHECK(lsqtest::waitFor([&] { return spy.count() >= 1; }, 2000));
}
