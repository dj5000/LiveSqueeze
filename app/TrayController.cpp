#include "TrayController.hpp"

#include <QActionGroup>
#include <QApplication>

#include "lsq/presets.hpp"

namespace lsqapp {

TrayController::TrayController(AppController* controller, MainWindow* window, QObject* parent)
    : QObject(parent), c_(controller), window_(window) {
    showAction_ = menu_.addAction(tr("Show LiveSqueeze"), this, &TrayController::showWindow);
    menu_.addSeparator();
    processingAction_ = menu_.addAction(tr("Processing"));
    processingAction_->setCheckable(true);
    connect(processingAction_, &QAction::triggered, c_, &AppController::setProcessingEnabled);
    bypassAction_ = menu_.addAction(tr("Bypass"));
    bypassAction_->setCheckable(true);
    connect(bypassAction_, &QAction::triggered, c_, &AppController::setBypass);
    presets_ = menu_.addMenu(tr("Preset"));
    connect(presets_, &QMenu::aboutToShow, this, &TrayController::rebuildPresets);
    menu_.addSeparator();
    menu_.addAction(tr("Quit LiveSqueeze"), qApp, &QApplication::quit);

    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, &TrayController::onActivated);
    connect(&menu_, &QMenu::aboutToShow, this, &TrayController::refresh);
    connect(c_, &AppController::stateChanged, this, &TrayController::onState);
    connect(c_, &AppController::paramsChanged, this, &TrayController::refresh);
    connect(c_, &AppController::settingsChanged, this, &TrayController::refresh);
    refresh();
}

bool TrayController::isAvailable() {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::show() {
    tray_.show();
}

void TrayController::showWindow() {
    window_->showNormal();
    window_->raise();
    window_->activateWindow();
}

void TrayController::toggleWindow() {
    if (window_->isVisible() && !window_->isMinimized()) {
        window_->hide();
    } else {
        showWindow();
    }
}

void TrayController::onActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        toggleWindow();
    }
}

void TrayController::refresh() {
    const int state = static_cast<int>(c_->state());
    const IconState icon = iconStateFor(state, c_->bypass(), c_->processingEnabled());
    tray_.setIcon(makeTrayIcon(icon));

    QString words;
    switch (icon) {
    case IconState::Running:
        words = tr("processing");
        break;
    case IconState::Bypassed:
        words = tr("bypassed");
        break;
    case IconState::Problem:
        words = tr("problem");
        break;
    case IconState::Stopped:
        words = tr("stopped");
        break;
    case IconState::Starting:
        words = tr("starting");
        break;
    }
    QString tip = tr("LiveSqueeze: %1").arg(words);
    const QString status = c_->statusText();
    if (!status.isEmpty() && icon != IconState::Stopped) {
        tip += QLatin1Char('\n') + status;
    }
    tray_.setToolTip(tip);

    processingAction_->setChecked(c_->processingEnabled());
    bypassAction_->setChecked(c_->bypass());
    showAction_->setText(window_->isVisible() ? tr("Hide LiveSqueeze") : tr("Show LiveSqueeze"));
}

void TrayController::onState(int state, const QString& text) {
    refresh();
    if (!tray_.isVisible() || window_->isVisible() || state == lastState_) {
        lastState_ = state;
        return;
    }
    // Tell the user about trouble and recovery, but only when they cannot see the window.
    const auto s = static_cast<lsq::SupervisorState>(state);
    const auto previous = static_cast<lsq::SupervisorState>(lastState_);
    if (s == lsq::SupervisorState::Failed || s == lsq::SupervisorState::Recovering) {
        tray_.showMessage(tr("LiveSqueeze"), text, QSystemTrayIcon::Warning, 6000);
    } else if (s == lsq::SupervisorState::Running &&
               (previous == lsq::SupervisorState::Recovering ||
                previous == lsq::SupervisorState::Failed)) {
        tray_.showMessage(tr("LiveSqueeze"), tr("Audio is back."), QSystemTrayIcon::Information,
                          3000);
    }
    lastState_ = state;
}

void TrayController::rebuildPresets() {
    presets_->clear();
    auto* group = new QActionGroup(presets_);
    for (std::size_t i = 0; i < lsq::kPresetCount; ++i) {
        const auto preset = static_cast<lsq::Preset>(i);
        const QString key = QString::fromLatin1(lsq::presetKey(preset));
        QAction* a = presets_->addAction(QString::fromLatin1(lsq::presetName(preset)));
        a->setCheckable(true);
        a->setActionGroup(group);
        a->setChecked(c_->presetKey() == key);
        connect(a, &QAction::triggered, c_, [this, key] { c_->setPreset(key); });
    }
    const QList<UserPreset> mine = c_->userPresets();
    if (!mine.isEmpty()) {
        presets_->addSeparator();
    }
    for (const UserPreset& u : mine) {
        QAction* a = presets_->addAction(tr("%1 (mine)").arg(u.name));
        a->setCheckable(true);
        a->setActionGroup(group);
        a->setChecked(c_->presetKey() == QStringLiteral("user:") + u.name);
        const QString name = u.name;
        connect(a, &QAction::triggered, c_, [this, name] { c_->applyUserPreset(name); });
    }
}

} // namespace lsqapp
