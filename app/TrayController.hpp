#pragma once

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

#include "AppController.hpp"
#include "Icons.hpp"
#include "MainWindow.hpp"

namespace lsqapp {

// The tray icon and its menu. The icon's outline says what is going on (see IconState), so it
// does not depend on colour, and the tooltip says it in words.
class TrayController : public QObject {
    Q_OBJECT
public:
    TrayController(AppController* controller, MainWindow* window, QObject* parent = nullptr);

    // False where the desktop has no tray (for example GNOME without an extension).
    static bool isAvailable();

    void show();
    QSystemTrayIcon* trayIcon() { return &tray_; }
    QMenu* menu() { return &menu_; }

public slots:
    void toggleWindow();
    void showWindow();

private slots:
    void refresh();
    void onState(int state, const QString& text);
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void rebuildPresets();

private:
    AppController* c_;
    MainWindow* window_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QMenu* presets_ = nullptr;
    QAction* showAction_ = nullptr;
    QAction* processingAction_ = nullptr;
    QAction* bypassAction_ = nullptr;
    int lastState_ = -1;
    bool shownCloseHint_ = false;
};

} // namespace lsqapp
