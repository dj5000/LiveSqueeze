#include "AppController.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QSysInfo>
#include <algorithm>

#include "lsq/db.hpp"
#include "lsq/version.hpp"

namespace lsqapp {
namespace {

constexpr int kVisibleMeterMs = 33; // 30 Hz while a window is open
constexpr int kHiddenMeterMs = 500; // 2 Hz in the tray
constexpr int kSaveDelayMs = 600;   // settings are written shortly after the last change

const char* kBackendName(const std::unique_ptr<lsq::IAudioBackend>& b) {
    return b ? b->name() : "none";
}

} // namespace

AppController::AppController() : AppController(Options{}) {}

AppController::AppController(const Options& options, QObject* parent)
    : QObject(parent), settings_(options.settingsPath), presets_(options.presetsPath),
      factory_(options.factory ? options.factory : lsq::BackendFactory([] {
          return lsq::createBackend(lsq::BackendKind::Auto);
      })),
      sup_(factory_, &store_) {
    qRegisterMetaType<lsqapp::MeterSnapshot>("lsqapp::MeterSnapshot");

    params_ = settings_.params();
    strength_ = settings_.strength();
    presetKey_ = settings_.preset();
    lsq::Preset known;
    if (!presetKey_.isEmpty() && !presetKey_.startsWith(QStringLiteral("user:")) &&
        !lsq::presetFromKey(presetKey_.toStdString(), known)) {
        presetKey_.clear();
    }
    processing_ = settings_.processingEnabled();
    store_.publish(params_);

    {
        const std::unique_ptr<lsq::IAudioBackend> probe = factory_();
        virtualSink_ = probe && probe->caps().createsVirtualSink;
    }

    // The supervisor reports from its worker thread; hop to this thread before touching the UI.
    sup_.setStateCallback([this](lsq::SupervisorState s, const std::string& text) {
        const QString t = QString::fromStdString(text);
        QMetaObject::invokeMethod(
            this, [this, s, t] { emit stateChanged(static_cast<int>(s), t); },
            Qt::QueuedConnection);
    });

    meterTimer_.setInterval(kHiddenMeterMs);
    connect(&meterTimer_, &QTimer::timeout, this, &AppController::pollMeters);
    meterTimer_.start();

    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(kSaveDelayMs);
    connect(&saveTimer_, &QTimer::timeout, this, &AppController::saveNow);

    if (options.autoStart && processing_) {
        restartAudio();
    }
}

AppController::~AppController() {
    saveNow();
    sup_.setStateCallback(nullptr);
    sup_.stop();
}

// ---- parameters and presets ---------------------------------------------------------------

double AppController::paramValue(const QString& key) const {
    const lsq::ParamDesc* d = lsq::findParam(key.toStdString());
    return d != nullptr ? static_cast<double>(lsq::paramGet(params_, *d)) : 0.0;
}

QList<UserPreset> AppController::userPresets() const {
    QList<UserPreset> out;
    for (UserPreset& p : presets_.load()) {
        out.push_back(std::move(p));
    }
    return out;
}

void AppController::publish() {
    store_.publish(params_);
    scheduleSave();
}

void AppController::scheduleSave() {
    saveTimer_.start();
}

void AppController::setParam(const QString& key, double value) {
    const lsq::ParamDesc* d = lsq::findParam(key.toStdString());
    if (d == nullptr) {
        return;
    }
    const float before = lsq::paramGet(params_, *d);
    lsq::paramSet(params_, *d, static_cast<float>(value));
    lsq::sanitize(params_);
    if (lsq::paramGet(params_, *d) == before) {
        return;
    }
    // The bypass switch and the volume are not part of what a preset or the strength describes.
    const bool partOfSound = key != QLatin1String("bypass") && key != QLatin1String("out_trim_db");
    if (partOfSound) {
        presetKey_.clear();
        strength_ = -1;
    }
    publish();
    emit paramsChanged();
    if (partOfSound) {
        emit presetChanged();
    }
}

namespace {

// Everything the user chose that a preset must not overwrite.
void keepUserChoices(lsq::Params& next, const lsq::Params& current) {
    next.bypass = current.bypass;
    next.outTrimDb = current.outTrimDb;
    next.lfeEnabled = current.lfeEnabled;
    next.lfeGainDb = current.lfeGainDb;
}

} // namespace

void AppController::setPreset(const QString& key) {
    lsq::Preset preset;
    if (!lsq::presetFromKey(key.toStdString(), preset)) {
        return;
    }
    lsq::Params next = lsq::presetParams(preset);
    keepUserChoices(next, params_);
    params_ = next;
    presetKey_ = key;
    strength_ = preset == lsq::Preset::MovieNight ? 50 : -1;
    publish();
    emit paramsChanged();
    emit presetChanged();
}

void AppController::applyUserPreset(const QString& name) {
    for (const UserPreset& p : presets_.load()) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            lsq::Params next = p.params;
            keepUserChoices(next, params_);
            lsq::sanitize(next);
            params_ = next;
            presetKey_ = QStringLiteral("user:") + p.name;
            strength_ = -1;
            publish();
            emit paramsChanged();
            emit presetChanged();
            return;
        }
    }
}

bool AppController::saveUserPreset(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    UserPreset p;
    p.name = trimmed;
    p.params = params_;
    p.params.bypass = false;
    if (!presets_.save(p)) {
        return false;
    }
    presetKey_ = QStringLiteral("user:") + trimmed;
    scheduleSave();
    emit presetChanged();
    return true;
}

bool AppController::deleteUserPreset(const QString& name) {
    const bool removed = presets_.remove(name);
    if (removed && presetKey_.compare(QStringLiteral("user:") + name, Qt::CaseInsensitive) == 0) {
        presetKey_.clear();
    }
    emit presetChanged();
    return removed;
}

void AppController::setStrength(int percent) {
    percent = std::clamp(percent, 0, 100);
    lsq::Params next = lsq::strengthParams(static_cast<float>(percent));
    keepUserChoices(next, params_);
    params_ = next;
    strength_ = percent;
    presetKey_ = percent == 50 ? QStringLiteral("movie-night") : QString();
    publish();
    emit paramsChanged();
    emit presetChanged();
}

void AppController::setBypass(bool bypass) {
    if (params_.bypass == bypass) {
        return;
    }
    params_.bypass = bypass;
    publish();
    emit paramsChanged();
}

// ---- audio -----------------------------------------------------------------------------------

lsq::SupervisorConfig AppController::buildConfig() const {
    lsq::SupervisorConfig cfg;
    cfg.request.captureId = settings_.inputDevice().toStdString();
    cfg.request.playbackId = settings_.outputDevice().toStdString();
    switch (settings_.latencyMode()) {
    case 0:
        cfg.latency = lsq::LatencyMode::Low;
        break;
    case 2:
        cfg.latency = lsq::LatencyMode::Safe;
        break;
    default:
        cfg.latency = lsq::LatencyMode::Balanced;
        break;
    }
    if (virtualSink_) {
        cfg.request.createVirtualSink = true;
        lsq::ChannelMap layout;
        if (lsq::ChannelMap::parse(settings_.sinkLayout().toStdString(), layout)) {
            cfg.request.virtualSinkLayout = layout;
        }
    }
    const QString inLayout = settings_.inputLayout();
    if (!inLayout.isEmpty()) {
        lsq::ChannelMap layout;
        if (lsq::ChannelMap::parse(inLayout.toStdString(), layout)) {
            cfg.captureLayout = layout;
        }
    }
    return cfg;
}

void AppController::restartAudio() {
    sup_.start(buildConfig());
}

void AppController::setProcessingEnabled(bool on) {
    if (processing_ == on) {
        return;
    }
    processing_ = on;
    settings_.setProcessingEnabled(on);
    if (on) {
        restartAudio();
    } else {
        sup_.stop();
        emit stateChanged(static_cast<int>(lsq::SupervisorState::Idle), statusText());
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setInputDevice(const QString& id) {
    if (settings_.inputDevice() == id) {
        return;
    }
    settings_.setInputDevice(id);
    if (processing_) {
        restartAudio();
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setOutputDevice(const QString& id) {
    if (settings_.outputDevice() == id) {
        return;
    }
    settings_.setOutputDevice(id);
    if (processing_) {
        restartAudio();
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setLatencyMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (settings_.latencyMode() == mode) {
        return;
    }
    settings_.setLatencyMode(mode);
    if (processing_) {
        restartAudio();
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setInputLayout(const QString& layout) {
    if (settings_.inputLayout() == layout) {
        return;
    }
    settings_.setInputLayout(layout);
    if (processing_) {
        restartAudio();
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setSinkLayout(const QString& layout) {
    if (settings_.sinkLayout() == layout) {
        return;
    }
    settings_.setSinkLayout(layout);
    if (processing_) {
        restartAudio();
    }
    scheduleSave();
    emit settingsChanged();
}

void AppController::setStartMinimized(bool on) {
    settings_.setStartMinimized(on);
    scheduleSave();
    emit settingsChanged();
}

QList<AppController::DeviceEntry> AppController::devices(bool input) {
    QList<DeviceEntry> out;
    for (const lsq::DeviceInfo& d :
         sup_.listDevices(input ? lsq::Dir::Capture : lsq::Dir::Playback)) {
        DeviceEntry e;
        e.id = QString::fromStdString(d.id);
        e.name = QString::fromStdString(d.name);
        e.isDefault = d.isDefault;
        e.looksVirtual = d.looksVirtual;
        out.push_back(e);
    }
    return out;
}

// ---- meters --------------------------------------------------------------------------------

void AppController::setMetersVisible(bool visible) {
    meterTimer_.setInterval(visible ? kVisibleMeterMs : kHiddenMeterMs);
}

void AppController::pollMeters() {
    MeterSnapshot snap;
    lsq::MeterFrame f;
    while (sup_.popMeter(f)) {
        snap.any = true;
        snap.inPeakDb = std::max(snap.inPeakDb, f.inPeakDb);
        snap.outPeakDb = std::max(snap.outPeakDb, f.outPeakDb);
        snap.reductionDb =
            std::min(snap.reductionDb, std::min(0.0f, f.compGainDb + f.limiterGainDb));
        snap.boostDb = std::max(snap.boostDb, f.compMaxGainDb);
        snap.levelDb = f.detectorLevelDb;
    }
    if (snap.any) {
        if (params_.bypass) {
            // The compressor keeps computing in the background, but nothing is being changed.
            snap.reductionDb = 0.0f;
            snap.boostDb = 0.0f;
        }
        emit metersUpdated(snap);
    }
}

// ---- persistence and diagnostics --------------------------------------------------------------

void AppController::saveNow() {
    saveTimer_.stop();
    settings_.setParams(params_);
    settings_.setStrength(strength_);
    settings_.setPreset(presetKey_);
    settings_.setProcessingEnabled(processing_);
    settings_.sync();
}

QString AppController::diagnostics() {
    QString t;
    auto line = [&t](const QString& s) { t += s + QLatin1Char('\n'); };
    line(QStringLiteral("LiveSqueeze %1").arg(QString::fromLatin1(lsq::versionString())));
    line(QStringLiteral("System: %1, Qt %2")
             .arg(QSysInfo::prettyProductName(), QString::fromLatin1(qVersion())));
    line(QStringLiteral("Time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    {
        const std::unique_ptr<lsq::IAudioBackend> b = factory_();
        line(QStringLiteral("Audio backend: %1").arg(QString::fromLatin1(kBackendName(b))));
    }
    line(QStringLiteral("State: %1 - %2")
             .arg(QString::fromLatin1(lsq::stateName(sup_.state())), statusText()));
    if (!lastError().isEmpty()) {
        line(QStringLiteral("Last error: %1").arg(lastError()));
    }
    line(QStringLiteral("Restarts: %1").arg(sup_.restarts()));

    const lsq::NegotiatedInfo n = sup_.negotiated();
    if (!n.captureName.empty() || !n.playbackName.empty()) {
        line(QStringLiteral("Input:  %1, %2 Hz, %3 channels (%4), period %5 frames")
                 .arg(QString::fromStdString(n.captureName))
                 .arg(n.captureRate)
                 .arg(n.captureMap.n)
                 .arg(QString::fromStdString(n.captureMap.toString()))
                 .arg(n.capturePeriodFrames));
        line(QStringLiteral("Output: %1, %2 Hz, period %3 frames")
                 .arg(QString::fromStdString(n.playbackName))
                 .arg(n.playbackRate)
                 .arg(n.playbackPeriodFrames));
        line(QStringLiteral("Shared clock: %1").arg(n.sharedClock ? "yes" : "no"));
    }
    const lsq::EngineStats s = sup_.stats();
    line(QStringLiteral("Engine: latency %1 ms, queue %2 / %3 ms, drift trim %4 ppm")
             .arg(s.latencyMs, 0, 'f', 1)
             .arg(s.fillMs, 0, 'f', 1)
             .arg(s.targetFillMs, 0, 'f', 1)
             .arg(s.trimPpm, 0, 'f', 1));
    line(QStringLiteral("Counters: underruns %1, overruns %2, overfill skips %3, restarts of "
                        "playback %4, queue enlargements %5")
             .arg(s.underruns)
             .arg(s.overruns)
             .arg(s.overfillSkips)
             .arg(s.primes)
             .arg(s.adaptations));

    line(QStringLiteral("Settings file: %1").arg(settings_.fileName()));
    line(QStringLiteral("Presets file: %1").arg(presets_.fileName()));
    line(QStringLiteral("Latency mode: %1, sink layout: %2, input layout: %3")
             .arg(settings_.latencyMode())
             .arg(settings_.sinkLayout(), settings_.inputLayout().isEmpty()
                                              ? QStringLiteral("(device)")
                                              : settings_.inputLayout()));

    for (const bool input : {true, false}) {
        line(input ? QStringLiteral("Input devices:") : QStringLiteral("Output devices:"));
        for (const DeviceEntry& d : devices(input)) {
            line(QStringLiteral("  %1%2%3 [%4]")
                     .arg(d.name, d.isDefault ? QStringLiteral(" (default)") : QString(),
                          d.looksVirtual ? QStringLiteral(" (virtual)") : QString(), d.id));
        }
    }

    line(QStringLiteral("Parameters (%1):")
             .arg(presetKey_.isEmpty() ? QStringLiteral("custom") : presetKey_));
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        line(QStringLiteral("  %1 = %2")
                 .arg(QString::fromLatin1(d.key))
                 .arg(static_cast<double>(lsq::paramGet(params_, d)), 0, 'g', 6));
    }
    return t;
}

} // namespace lsqapp
