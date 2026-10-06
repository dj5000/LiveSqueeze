#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <map>

#include "AppController.hpp"
#include "Widgets.hpp"

namespace lsqapp {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);

    // With a system tray, closing the window hides it; without one, closing quits.
    void setTrayAvailable(bool available) { trayAvailable_ = available; }

    // The size the window opens at on a large enough screen.
    QSize preferredSize() const;

    // Parts the tests look at.
    QTabWidget* tabs() const { return tabs_; }
    QSlider* strengthSlider() const { return strength_; }
    QComboBox* presetCombo() const { return preset_; }
    QCheckBox* bypassCheck() const { return bypass_; }
    QPushButton* processingButton() const { return processing_; }
    QLabel* statusLabel() const { return status_; }
    ParamRow* row(const QString& key) const;         // on the Simple page
    ParamRow* advancedRow(const QString& key) const; // on the Advanced page
    LevelMeter* inputMeter() const { return inMeter_; }
    LevelMeter* outputMeter() const { return outMeter_; }
    GainMeter* gainMeter() const { return gainMeter_; }
    CurveView* curve() const { return curve_; }
    QComboBox* outputCombo() const { return outputDevice_; }
    QComboBox* inputCombo() const { return inputDevice_; }

signals:
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private slots:
    void onParamsChanged();
    void onPresetChanged();
    void onState(int state, const QString& text);
    void onMeters(const lsqapp::MeterSnapshot& m);
    void refreshStats();
    void refreshDevices();
    void showDiagnostics();
    void saveUserPreset();
    void deleteUserPreset();

private:
    QWidget* buildSimpleTab();
    QWidget* buildAdvancedTab();
    QWidget* buildDevicesTab();
    void refreshPresetCombo();
    void fillDevices(QComboBox* combo, bool input, const QString& current);
    void updateStatus(int state, const QString& text);

    AppController* c_;
    bool trayAvailable_ = false;
    bool updating_ = false;
    int lastState_ = 0;
    QString lastText_;

    QLabel* status_ = nullptr;
    QLabel* stats_ = nullptr;
    QPushButton* processing_ = nullptr;
    QCheckBox* bypass_ = nullptr;
    LevelMeter* inMeter_ = nullptr;
    LevelMeter* outMeter_ = nullptr;
    GainMeter* gainMeter_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    QSlider* strength_ = nullptr;
    QLabel* strengthValue_ = nullptr;
    QComboBox* preset_ = nullptr;
    QPushButton* savePreset_ = nullptr;
    QPushButton* deletePreset_ = nullptr;
    CurveView* curve_ = nullptr;
    QLabel* curveSummary_ = nullptr;

    QComboBox* outputDevice_ = nullptr;
    QComboBox* inputDevice_ = nullptr;
    QComboBox* latency_ = nullptr;
    QComboBox* sinkLayout_ = nullptr;
    QComboBox* inputLayout_ = nullptr;

    std::map<QString, ParamRow*> rows_;         // the Simple page
    std::map<QString, ParamRow*> advancedRows_; // the Advanced page
    QTimer statsTimer_;
};

} // namespace lsqapp
