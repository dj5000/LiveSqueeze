#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <cstdio>

#include "AppController.hpp"
#include "Icons.hpp"
#include "MainWindow.hpp"
#include "SingleInstance.hpp"
#include "TrayController.hpp"
#include "lsq/backend_factory.hpp"
#include "lsq/version.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LiveSqueeze"));
    QApplication::setOrganizationName(QStringLiteral("LiveSqueeze"));
    QApplication::setApplicationVersion(QString::fromLatin1(lsq::versionString()));
    QApplication::setWindowIcon(lsqapp::makeAppIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Realtime audio compressor and limiter: tames loud sounds and lifts "
                       "quiet ones."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized(QStringLiteral("minimized"),
                                 QStringLiteral("Start hidden in the system tray."));
    QCommandLineOption settings(QStringLiteral("settings"),
                                QStringLiteral("Use this settings file."), QStringLiteral("file"));
    QCommandLineOption backend(QStringLiteral("backend"),
                               QStringLiteral("Audio backend: auto (default), miniaudio, "
                                              "pipewire, null or fake (simulated, for demos)."),
                               QStringLiteral("name"), QStringLiteral("auto"));
    QCommandLineOption noAudio(QStringLiteral("no-audio"),
                               QStringLiteral("Do not start processing at launch."));
    parser.addOption(minimized);
    parser.addOption(settings);
    parser.addOption(backend);
    parser.addOption(noAudio);
    parser.process(app);

    // One copy per user: a second launch just shows the first one's window.
    lsqapp::SingleInstance instance(
        QStringLiteral("LiveSqueeze-") +
        QString::fromLocal8Bit(qgetenv("USER").isEmpty() ? qgetenv("USERNAME") : qgetenv("USER")));
    if (!instance.isPrimary()) {
        if (instance.notifyPrimary()) {
            return 0;
        }
        // The lock is held but nobody answers: carry on rather than leave the user without a
        // program.
    }

    lsqapp::AppController::Options options;
    options.settingsPath = parser.value(settings);
    options.autoStart = !parser.isSet(noAudio);
    lsq::BackendKind kind = lsq::BackendKind::Auto;
    if (!lsq::parseBackendKind(parser.value(backend).toStdString(), kind) ||
        !lsq::backendAvailable(kind)) {
        std::fprintf(stderr, "livesqueeze: backend '%s' is not available in this build\n",
                     parser.value(backend).toLocal8Bit().constData());
        return 2;
    }
    options.factory = [kind] { return lsq::createBackend(kind); };

    lsqapp::AppController controller(options);
    lsqapp::MainWindow window(&controller);

    const bool trayAvailable = lsqapp::TrayController::isAvailable();
    window.setTrayAvailable(trayAvailable);
    app.setQuitOnLastWindowClosed(!trayAvailable);
    QObject::connect(&window, &lsqapp::MainWindow::quitRequested, &app, &QApplication::quit);

    lsqapp::TrayController tray(&controller, &window);
    if (trayAvailable) {
        tray.show();
    }
    QObject::connect(&instance, &lsqapp::SingleInstance::showRequested, &tray,
                     &lsqapp::TrayController::showWindow);

    // Start hidden only if there is a tray to find the program again; otherwise the window is the
    // only way in.
    const bool hidden = trayAvailable && (parser.isSet(minimized) || controller.startMinimized());
    if (!hidden) {
        window.show();
    }
    return app.exec();
}
