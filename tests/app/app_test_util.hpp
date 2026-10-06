#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <functional>

#include "AppController.hpp"
#include "lsq/backend_factory.hpp"

namespace lsqtest {

// Runs the event loop until `done()` is true or the time is up.
inline bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// A controller on simulated devices and private settings files.
struct Fixture {
    QTemporaryDir dir;
    lsqapp::AppController::Options options;

    explicit Fixture(bool autoStart = true) {
        options.settingsPath = dir.filePath(QStringLiteral("settings.ini"));
        options.presetsPath = dir.filePath(QStringLiteral("presets.json"));
        options.factory = [] { return lsq::createBackend(lsq::BackendKind::Fake); };
        options.autoStart = autoStart;
    }
};

} // namespace lsqtest
