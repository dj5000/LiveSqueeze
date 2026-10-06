#define DOCTEST_CONFIG_IMPLEMENT
#include <QAccessible>
#include <QApplication>
#include <QtGlobal>

#include "doctest.h"

// The tests need no display: Qt's "offscreen" platform draws into memory.
int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // Keep the user's real settings and autostart entries out of the tests.
    qputenv("XDG_CONFIG_HOME", "/nonexistent-lsq-test-config");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LiveSqueeze-test"));
    QApplication::setOrganizationName(QStringLiteral("LiveSqueeze-test"));
    QAccessible::setActive(true);
    doctest::Context context(argc, argv);
    return context.run();
}
