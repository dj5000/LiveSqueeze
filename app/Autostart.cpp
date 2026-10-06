#include "Autostart.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace lsqapp::autostart {

#if defined(Q_OS_WIN)

static const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

QString entryLocation() {
    return QString::fromLatin1(kRunKey) + QStringLiteral("\\LiveSqueeze");
}

bool isEnabled() {
    QSettings s(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return s.contains(QStringLiteral("LiveSqueeze"));
}

bool setEnabled(bool enabled, const QString& executablePath) {
    QSettings s(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString exe = QDir::toNativeSeparators(executablePath);
        s.setValue(QStringLiteral("LiveSqueeze"), QStringLiteral("\"%1\" --minimized").arg(exe));
    } else {
        s.remove(QStringLiteral("LiveSqueeze"));
    }
    s.sync();
    return s.status() == QSettings::NoError;
}

#elif defined(Q_OS_MACOS)

QString entryLocation() {
    return QDir::home().filePath(QStringLiteral("Library/LaunchAgents/com.livesqueeze.app.plist"));
}

bool isEnabled() {
    return QFileInfo::exists(entryLocation());
}

bool setEnabled(bool enabled, const QString& executablePath) {
    const QString path = entryLocation();
    if (!enabled) {
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\"><dict>\n"
           "  <key>Label</key><string>com.livesqueeze.app</string>\n"
           "  <key>ProgramArguments</key><array><string>"
        << executablePath << "</string><string>--minimized</string></array>\n"
        << "  <key>RunAtLoad</key><true/>\n"
           "</dict></plist>\n";
    out.flush();
    return f.commit();
}

#else // Linux and other XDG desktops

QString entryLocation() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(dir).filePath(QStringLiteral("autostart/livesqueeze.desktop"));
}

bool isEnabled() {
    return QFileInfo::exists(entryLocation());
}

bool setEnabled(bool enabled, const QString& executablePath) {
    const QString path = entryLocation();
    if (!enabled) {
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
    out << "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=LiveSqueeze\n"
           "Comment=Realtime audio compressor and limiter\n"
           "Exec=\""
        << executablePath
        << "\" --minimized\n"
           "Terminal=false\n"
           "X-GNOME-Autostart-enabled=true\n";
    out.flush();
    return f.commit();
}

#endif

} // namespace lsqapp::autostart
