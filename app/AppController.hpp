#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

#include "PresetStore.hpp"
#include "Settings.hpp"
#include "lsq/backend_factory.hpp"
#include "lsq/params.hpp"
#include "lsq/presets.hpp"
#include "lsq/supervisor.hpp"

namespace lsqapp {

// One snapshot of the meters, in dB, accumulated over the last update interval.
struct MeterSnapshot {
    float inPeakDb = -120.0f;
    float outPeakDb = -120.0f;
    float reductionDb = 0.0f; // <= 0: how far the compressor and limiter turned things down
    float boostDb = 0.0f;     // >= 0: how far the compressor turned quiet things up
    float levelDb = -120.0f;  // what the compressor's detector last measured
    bool any = false;         // false if no new data arrived
};

// Everything the user interface needs, without any widgets: parameters, presets, devices, the
// running audio engine and its status. The widgets only display this and call its slots, so it
// can be tested with a simulated audio backend and no display.
class AppController : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString settingsPath;        // empty: the standard per-user location
        QString presetsPath;         // empty: the standard per-user location
        lsq::BackendFactory factory; // empty: the platform's best backend
        bool autoStart = true;       // start processing right away if it was enabled last time
    };

    struct DeviceEntry {
        QString id;
        QString name;
        bool isDefault = false;
        bool looksVirtual = false;
    };

    AppController();
    explicit AppController(const Options& options, QObject* parent = nullptr);
    ~AppController() override;

    // --- parameters and presets
    lsq::Params params() const { return params_; }
    double paramValue(const QString& key) const;
    int strength() const { return strength_; }       // 0..100, -1 = custom
    QString presetKey() const { return presetKey_; } // "" = custom
    bool bypass() const { return params_.bypass; }
    QList<UserPreset> userPresets() const;

    // --- audio state
    lsq::SupervisorState state() const { return sup_.state(); }
    QString statusText() const { return QString::fromStdString(sup_.statusText()); }
    QString lastError() const { return QString::fromStdString(sup_.lastError()); }
    lsq::EngineStats engineStats() const { return sup_.stats(); }
    bool processingEnabled() const { return processing_; }
    bool createsVirtualSink() const { return virtualSink_; }

    // --- devices and settings
    QList<DeviceEntry> devices(bool input);
    QString inputDevice() const { return settings_.inputDevice(); }
    QString outputDevice() const { return settings_.outputDevice(); }
    int latencyMode() const { return settings_.latencyMode(); }
    QString inputLayout() const { return settings_.inputLayout(); }
    QString sinkLayout() const { return settings_.sinkLayout(); }
    bool startMinimized() const { return settings_.startMinimized(); }

    QString diagnostics();

    Settings& settings() { return settings_; }
    PresetStore& presetStore() { return presets_; }
    lsq::Supervisor& supervisor() { return sup_; }

public slots:
    void setParam(const QString& key, double value);
    void setPreset(const QString& presetKey); // a built-in preset key
    void applyUserPreset(const QString& name);
    bool saveUserPreset(const QString& name); // saves the current parameters
    bool deleteUserPreset(const QString& name);
    void setStrength(int percent);
    void setBypass(bool bypass);
    void setProcessingEnabled(bool on);
    void setInputDevice(const QString& id);
    void setOutputDevice(const QString& id);
    void setLatencyMode(int mode);
    void setInputLayout(const QString& layout);
    void setSinkLayout(const QString& layout);
    void setStartMinimized(bool on);

    // Meters are polled fast while a window is visible and slowly otherwise.
    void setMetersVisible(bool visible);
    void saveNow(); // flush settings to disk immediately

signals:
    void paramsChanged();
    void presetChanged();
    void stateChanged(int state, const QString& statusText);
    void metersUpdated(const lsqapp::MeterSnapshot& meters);
    void devicesChanged();
    void settingsChanged();

private:
    void publish();
    void restartAudio();
    lsq::SupervisorConfig buildConfig() const;
    void pollMeters();
    void scheduleSave();

    Settings settings_;
    PresetStore presets_;
    lsq::BackendFactory factory_;
    lsq::ParamStore store_;
    lsq::Supervisor sup_;
    lsq::Params params_;
    int strength_ = 50;
    QString presetKey_;
    bool processing_ = true;
    bool virtualSink_ = false;
    QTimer meterTimer_;
    QTimer saveTimer_;
};

} // namespace lsqapp

Q_DECLARE_METATYPE(lsqapp::MeterSnapshot)
