#pragma once

#include <QSettings>
#include <QString>
#include <memory>

#include "lsq/params.hpp"

namespace lsqapp {

// Everything the app remembers between runs, in one INI file. Corrupt or missing values fall back
// to defaults instead of failing.
class Settings {
public:
    // `path` empty: the platform's standard per-user location.
    explicit Settings(const QString& path = {});

    lsq::Params params() const;
    void setParams(const lsq::Params& p);

    int strength() const; // 0..100, or -1 for "custom"
    void setStrength(int percent);
    QString preset() const; // preset key, or empty
    void setPreset(const QString& key);

    QString inputDevice() const; // device id, empty = automatic
    void setInputDevice(const QString& id);
    QString outputDevice() const;
    void setOutputDevice(const QString& id);

    int latencyMode() const; // 0 low, 1 balanced, 2 safe
    void setLatencyMode(int mode);
    QString inputLayout() const; // "" = what the device reports
    void setInputLayout(const QString& layout);
    QString sinkLayout() const; // virtual sink layout, "5.1" or "7.1"
    void setSinkLayout(const QString& layout);

    bool processingEnabled() const;
    void setProcessingEnabled(bool on);
    bool startMinimized() const;
    void setStartMinimized(bool on);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& g);

    void sync() { settings_->sync(); }
    QString fileName() const { return settings_->fileName(); }

private:
    std::unique_ptr<QSettings> settings_;
};

} // namespace lsqapp
