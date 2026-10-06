#include "Settings.hpp"

#include <QStandardPaths>

#include "lsq/presets.hpp"

namespace lsqapp {

Settings::Settings(const QString& path) {
    if (path.isEmpty()) {
        settings_ = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                                QStringLiteral("LiveSqueeze"),
                                                QStringLiteral("LiveSqueeze"));
    } else {
        settings_ = std::make_unique<QSettings>(path, QSettings::IniFormat);
    }
}

lsq::Params Settings::params() const {
    lsq::Params p = lsq::presetParams(lsq::Preset::MovieNight);
    settings_->beginGroup(QStringLiteral("params"));
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        const QVariant v = settings_->value(QString::fromLatin1(d.key));
        bool ok = false;
        const double x = v.toDouble(&ok);
        if (v.isValid() && ok) {
            lsq::paramSet(p, d, static_cast<float>(x));
        } else if (v.isValid() && d.kind == lsq::ParamKind::Bool) {
            lsq::paramSet(p, d, v.toBool() ? 1.0f : 0.0f);
        }
    }
    settings_->endGroup();
    lsq::sanitize(p);
    return p;
}

void Settings::setParams(const lsq::Params& p) {
    settings_->beginGroup(QStringLiteral("params"));
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        settings_->setValue(QString::fromLatin1(d.key), static_cast<double>(lsq::paramGet(p, d)));
    }
    settings_->endGroup();
}

int Settings::strength() const {
    bool ok = false;
    const int v = settings_->value(QStringLiteral("ui/strength"), 50).toInt(&ok);
    return ok && v >= -1 && v <= 100 ? v : 50;
}
void Settings::setStrength(int percent) {
    settings_->setValue(QStringLiteral("ui/strength"), percent);
}

QString Settings::preset() const {
    return settings_->value(QStringLiteral("ui/preset"), QStringLiteral("movie-night")).toString();
}
void Settings::setPreset(const QString& key) {
    settings_->setValue(QStringLiteral("ui/preset"), key);
}

QString Settings::inputDevice() const {
    return settings_->value(QStringLiteral("devices/input")).toString();
}
void Settings::setInputDevice(const QString& id) {
    settings_->setValue(QStringLiteral("devices/input"), id);
}
QString Settings::outputDevice() const {
    return settings_->value(QStringLiteral("devices/output")).toString();
}
void Settings::setOutputDevice(const QString& id) {
    settings_->setValue(QStringLiteral("devices/output"), id);
}

int Settings::latencyMode() const {
    bool ok = false;
    const int v = settings_->value(QStringLiteral("devices/latency"), 1).toInt(&ok);
    return ok && v >= 0 && v <= 2 ? v : 1;
}
void Settings::setLatencyMode(int mode) {
    settings_->setValue(QStringLiteral("devices/latency"), mode);
}

QString Settings::inputLayout() const {
    return settings_->value(QStringLiteral("devices/input_layout")).toString();
}
void Settings::setInputLayout(const QString& layout) {
    settings_->setValue(QStringLiteral("devices/input_layout"), layout);
}
QString Settings::sinkLayout() const {
    return settings_->value(QStringLiteral("devices/sink_layout"), QStringLiteral("5.1"))
        .toString();
}
void Settings::setSinkLayout(const QString& layout) {
    settings_->setValue(QStringLiteral("devices/sink_layout"), layout);
}

bool Settings::processingEnabled() const {
    return settings_->value(QStringLiteral("ui/processing"), true).toBool();
}
void Settings::setProcessingEnabled(bool on) {
    settings_->setValue(QStringLiteral("ui/processing"), on);
}
bool Settings::startMinimized() const {
    return settings_->value(QStringLiteral("ui/start_minimized"), false).toBool();
}
void Settings::setStartMinimized(bool on) {
    settings_->setValue(QStringLiteral("ui/start_minimized"), on);
}

QByteArray Settings::windowGeometry() const {
    return settings_->value(QStringLiteral("ui/geometry")).toByteArray();
}
void Settings::setWindowGeometry(const QByteArray& g) {
    settings_->setValue(QStringLiteral("ui/geometry"), g);
}

} // namespace lsqapp
