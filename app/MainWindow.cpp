#include "MainWindow.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Autostart.hpp"
#include "Icons.hpp"
#include "lsq/presets.hpp"

namespace lsqapp {
namespace {

const char* groupTitle(const QString& group) {
    if (group == QLatin1String("downmix")) {
        return "Downmix to stereo";
    }
    if (group == QLatin1String("compressor")) {
        return "Compressor";
    }
    if (group == QLatin1String("limiter")) {
        return "Limiter";
    }
    return "Output";
}

QString stateWords(int state) {
    switch (static_cast<lsq::SupervisorState>(state)) {
    case lsq::SupervisorState::Running:
        return MainWindow::tr("Running");
    case lsq::SupervisorState::Starting:
        return MainWindow::tr("Starting");
    case lsq::SupervisorState::Recovering:
        return MainWindow::tr("Reconnecting");
    case lsq::SupervisorState::Failed:
        return MainWindow::tr("Problem");
    case lsq::SupervisorState::Idle:
        break;
    }
    return MainWindow::tr("Stopped");
}

} // namespace

MainWindow::MainWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent), c_(controller) {
    setWindowTitle(tr("LiveSqueeze"));
    setWindowIcon(makeAppIcon());

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // ---- status row
    auto* top = new QHBoxLayout;
    status_ = new QLabel(central);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = status_->font();
    bold.setBold(true);
    status_->setFont(bold);
    status_->setAccessibleName(tr("Status"));
    top->addWidget(status_, 1);

    processing_ = new QPushButton(central);
    processing_->setCheckable(true);
    processing_->setAccessibleDescription(
        tr("Starts or stops LiveSqueeze. While it is stopped, "
           "sound sent to the LiveSqueeze device is not played."));
    top->addWidget(processing_);

    bypass_ = new QCheckBox(tr("&Bypass"), central);
    bypass_->setToolTip(paramHelp(QStringLiteral("bypass")));
    bypass_->setAccessibleName(tr("Bypass"));
    bypass_->setAccessibleDescription(paramHelp(QStringLiteral("bypass")));
    top->addWidget(bypass_);
    root->addLayout(top);

    stats_ = new QLabel(central);
    stats_->setAccessibleName(tr("Engine details"));
    root->addWidget(stats_);

    // ---- meters
    auto* meters = new QGroupBox(tr("Levels"), central);
    auto* form = new QFormLayout(meters);
    inMeter_ = new LevelMeter(tr("Input level"), meters);
    outMeter_ = new LevelMeter(tr("Output level"), meters);
    gainMeter_ = new GainMeter(tr("Gain change"), meters);
    form->addRow(tr("In"), inMeter_);
    form->addRow(tr("Out"), outMeter_);
    form->addRow(tr("Gain"), gainMeter_);
    root->addWidget(meters);

    // ---- tabs
    tabs_ = new QTabWidget(central);
    tabs_->addTab(buildSimpleTab(), tr("&Simple"));
    tabs_->addTab(buildAdvancedTab(), tr("&Advanced"));
    tabs_->addTab(buildDevicesTab(), tr("&Devices"));
    root->addWidget(tabs_, 1);

    setCentralWidget(central);

    // ---- wiring
    connect(processing_, &QPushButton::toggled, c_, &AppController::setProcessingEnabled);
    connect(bypass_, &QCheckBox::toggled, c_, [this](bool on) {
        if (!updating_) {
            c_->setBypass(on);
        }
    });
    connect(c_, &AppController::paramsChanged, this, &MainWindow::onParamsChanged);
    connect(c_, &AppController::presetChanged, this, &MainWindow::onPresetChanged);
    connect(c_, &AppController::stateChanged, this, &MainWindow::onState);
    connect(c_, &AppController::metersUpdated, this, &MainWindow::onMeters);
    connect(c_, &AppController::settingsChanged, this, [this] {
        updating_ = true;
        processing_->setChecked(c_->processingEnabled());
        updating_ = false;
        updateStatus(lastState_, lastText_);
    });

    statsTimer_.setInterval(1000);
    connect(&statsTimer_, &QTimer::timeout, this, &MainWindow::refreshStats);

    // The first state: whatever the supervisor already reports.
    lastState_ = static_cast<int>(c_->state());
    lastText_ = c_->statusText();
    onParamsChanged();
    onPresetChanged();
    processing_->setChecked(c_->processingEnabled());
    updateStatus(lastState_, lastText_);
    refreshStats();

    // A sensible first size: the Simple page fits without scrolling at the current text size,
    // unless the screen is too small for that.
    QSize want = preferredSize();
    if (const QScreen* s = QGuiApplication::primaryScreen()) {
        const QSize room = s->availableGeometry().size() * 9 / 10;
        want = want.boundedTo(room);
    }
    resize(want);
}

QSize MainWindow::preferredSize() const {
    const QFontMetrics fm = fontMetrics();
    return {fm.horizontalAdvance(QStringLiteral("M")) * 62, fm.height() * 54};
}

ParamRow* MainWindow::row(const QString& key) const {
    const auto it = rows_.find(key);
    return it != rows_.end() ? it->second : nullptr;
}

ParamRow* MainWindow::advancedRow(const QString& key) const {
    const auto it = advancedRows_.find(key);
    return it != advancedRows_.end() ? it->second : nullptr;
}

// ---- tabs ----------------------------------------------------------------------------------

QWidget* MainWindow::buildSimpleTab() {
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);

    // Strength
    auto* strengthBox = new QGroupBox(tr("How much to even out the volume"), page);
    auto* sl = new QVBoxLayout(strengthBox);
    strength_ = new QSlider(Qt::Horizontal, strengthBox);
    strength_->setRange(0, 100);
    strength_->setTickPosition(QSlider::TicksBelow);
    strength_->setTickInterval(10);
    strength_->setPageStep(10);
    strength_->setAccessibleName(tr("Strength"));
    strength_->setAccessibleDescription(
        tr("From 0, only loud peaks are caught, to 100, quiet sounds are lifted and loud sounds "
           "pressed down as much as possible. 50 suits most films."));
    strength_->setToolTip(strength_->accessibleDescription());
    strengthValue_ = new QLabel(strengthBox);
    strengthValue_->setAccessibleName(tr("Strength value"));
    // Gentle and Strong sit at the ends of the slider; what they mean is one wrapped label below
    // (a single label in a vertical layout, which every platform sizes correctly).
    auto* sliderRow = new QHBoxLayout;
    sliderRow->addWidget(new QLabel(tr("Gentle"), strengthBox));
    sliderRow->addWidget(strength_, 1);
    sliderRow->addWidget(new QLabel(tr("Strong"), strengthBox));
    auto* strengthHelp =
        new QLabel(tr("Gentle only stops loud peaks. Strong is for late-night listening: quiet "
                      "sounds are lifted and loud ones pressed down as far as possible."),
                   strengthBox);
    strengthHelp->setWordWrap(true);
    sl->addWidget(strengthValue_);
    sl->addLayout(sliderRow);
    sl->addWidget(strengthHelp);
    grid->addWidget(strengthBox, 0, 0);

    // Preset
    auto* presetBox = new QGroupBox(tr("Preset"), page);
    auto* pl = new QGridLayout(presetBox);
    preset_ = new QComboBox(presetBox);
    preset_->setAccessibleName(tr("Preset"));
    preset_->setAccessibleDescription(tr("Ready-made settings. Choose one, then fine-tune on "
                                         "the Advanced page if you like."));
    auto* presetLabel = new QLabel(tr("Pr&eset:"), presetBox);
    presetLabel->setBuddy(preset_);
    savePreset_ = new QPushButton(tr("Save as…"), presetBox);
    savePreset_->setAccessibleDescription(tr("Saves the current settings as your own preset."));
    deletePreset_ = new QPushButton(tr("Delete"), presetBox);
    deletePreset_->setAccessibleDescription(tr("Deletes the selected preset of your own."));
    pl->addWidget(presetLabel, 0, 0);
    pl->addWidget(preset_, 0, 1, 1, 2);
    pl->addWidget(savePreset_, 1, 1);
    pl->addWidget(deletePreset_, 1, 2);
    grid->addWidget(presetBox, 1, 0);

    // Dialogue and volume
    auto* quick = new QGroupBox(tr("Quick adjustments"), page);
    auto* ql = new QVBoxLayout(quick);
    for (const char* key : {"center_gain_db", "out_trim_db"}) {
        const lsq::ParamDesc* d = lsq::findParam(key);
        auto* r = new ParamRow(*d, quick);
        connect(r, &ParamRow::valueEdited, c_, &AppController::setParam);
        rows_[QString::fromLatin1(key)] = r;
        ql->addWidget(r);
    }
    grid->addWidget(quick, 2, 0);

    // The curve
    auto* curveBox = new QGroupBox(tr("What it does"), page);
    auto* cl = new QVBoxLayout(curveBox);
    curve_ = new CurveView(curveBox);
    curveSummary_ = new QLabel(curveBox);
    curveSummary_->setWordWrap(true);
    curveSummary_->setAccessibleName(tr("Summary"));
    cl->addWidget(curve_, 1);
    cl->addWidget(curveSummary_);
    grid->addWidget(curveBox, 0, 1, 3, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(3, 1);

    connect(strength_, &QSlider::valueChanged, this, [this](int v) {
        if (!updating_) {
            c_->setStrength(v);
        }
    });
    connect(preset_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const QString key = preset_->itemData(index).toString();
        if (key.startsWith(QStringLiteral("user:"))) {
            c_->applyUserPreset(key.mid(5));
        } else if (!key.isEmpty()) {
            c_->setPreset(key);
        }
    });
    connect(savePreset_, &QPushButton::clicked, this, &MainWindow::saveUserPreset);
    connect(deletePreset_, &QPushButton::clicked, this, &MainWindow::deleteUserPreset);
    // In a scroll area like the Advanced page: with large text or a small screen the page scrolls
    // instead of cutting text off.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildAdvancedTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    QString group;
    QGroupBox* box = nullptr;
    QVBoxLayout* boxLayout = nullptr;
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        const QString key = QString::fromLatin1(d.key);
        if (key == QLatin1String("bypass")) {
            continue; // lives next to the status line
        }
        if (QString::fromLatin1(d.group) != group) {
            group = QString::fromLatin1(d.group);
            box = new QGroupBox(tr(groupTitle(group)), page);
            boxLayout = new QVBoxLayout(box);
            layout->addWidget(box);
        }
        auto* r = new ParamRow(d, box);
        connect(r, &ParamRow::valueEdited, c_, &AppController::setParam);
        advancedRows_[key] = r;
        boxLayout->addWidget(r);
    }
    layout->addStretch(1);
    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildDevicesTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;

    outputDevice_ = new QComboBox(page);
    outputDevice_->setAccessibleName(tr("Output device"));
    outputDevice_->setAccessibleDescription(tr("Your speakers or headphones. Automatic uses the "
                                               "system's own choice."));
    form->addRow(tr("&Output (speakers or headphones):"), outputDevice_);

    const bool ownDevice = c_->createsVirtualSink();
    if (!ownDevice) {
        inputDevice_ = new QComboBox(page);
        inputDevice_->setAccessibleName(tr("Input device"));
        inputDevice_->setAccessibleDescription(tr("The virtual audio cable that carries the "
                                                  "sound of your player to LiveSqueeze."));
        form->addRow(tr("&Input (virtual cable):"), inputDevice_);
    }

    latency_ = new QComboBox(page);
    latency_->addItem(tr("Low delay (about 25 ms)"), 0);
    latency_->addItem(tr("Balanced (about 45 ms)"), 1);
    latency_->addItem(tr("Safest (about 90 ms)"), 2);
    latency_->setAccessibleName(tr("Delay"));
    latency_->setAccessibleDescription(tr("How much delay LiveSqueeze adds. A longer delay "
                                          "survives a busy computer better but video and sound "
                                          "may drift apart."));
    form->addRow(tr("De&lay:"), latency_);

    if (ownDevice) {
        sinkLayout_ = new QComboBox(page);
        sinkLayout_->addItem(tr("5.1 surround"), QStringLiteral("5.1"));
        sinkLayout_->addItem(tr("7.1 surround"), QStringLiteral("7.1"));
        sinkLayout_->addItem(tr("Stereo"), QStringLiteral("stereo"));
        sinkLayout_->setAccessibleName(tr("Channels of the LiveSqueeze device"));
        sinkLayout_->setAccessibleDescription(tr("How many channels players can send to the "
                                                 "LiveSqueeze device."));
        form->addRow(tr("&Channels of the LiveSqueeze device:"), sinkLayout_);
    } else {
        inputLayout_ = new QComboBox(page);
        inputLayout_->addItem(tr("As the device reports"), QString());
        inputLayout_->addItem(tr("5.1 surround"), QStringLiteral("5.1"));
        inputLayout_->addItem(tr("7.1 surround"), QStringLiteral("7.1"));
        inputLayout_->addItem(tr("Stereo"), QStringLiteral("stereo"));
        inputLayout_->setAccessibleName(tr("Channel layout of the input"));
        inputLayout_->setAccessibleDescription(tr("Which speakers the first channels of the "
                                                  "input carry. Needed for cables that cannot "
                                                  "say, such as 16 channel BlackHole."));
        form->addRow(tr("Channel la&yout of the input:"), inputLayout_);
    }
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("&Refresh device list"), page);
    auto* diag = new QPushButton(tr("Diagnostics…"), page);
    diag->setAccessibleDescription(tr("Shows details to copy into a bug report."));
    buttons->addWidget(refresh);
    buttons->addWidget(diag);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto* login = new QCheckBox(tr("Start LiveSqueeze when I log in"), page);
    login->setChecked(autostart::isEnabled());
    auto* minimized = new QCheckBox(tr("Start hidden in the tray"), page);
    minimized->setChecked(c_->startMinimized());
    layout->addWidget(login);
    layout->addWidget(minimized);

    auto* help = new QLabel(page);
    help->setWordWrap(true);
    help->setTextFormat(Qt::PlainText);
    help->setText(ownDevice
                      ? tr("LiveSqueeze creates its own audio device called \"LiveSqueeze\". "
                           "Choose it as the output in your player or in the system sound "
                           "settings, and keep its volume at 100%.")
                      : tr("Choose your virtual audio cable as the output of your player or of "
                           "the system, set its volume to 100%, and choose the same cable as the "
                           "input here."));
    layout->addWidget(help);
    layout->addStretch(1);

    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(diag, &QPushButton::clicked, this, &MainWindow::showDiagnostics);
    connect(login, &QCheckBox::toggled, this, [login](bool on) {
        if (!autostart::setEnabled(on, QCoreApplication::applicationFilePath())) {
            QSignalBlocker block(login);
            login->setChecked(!on);
            QMessageBox::warning(login, tr("LiveSqueeze"),
                                 tr("Could not change the start-up setting."));
        }
    });
    connect(minimized, &QCheckBox::toggled, c_, &AppController::setStartMinimized);
    connect(outputDevice_, qOverload<int>(&QComboBox::activated), this,
            [this](int i) { c_->setOutputDevice(outputDevice_->itemData(i).toString()); });
    if (inputDevice_ != nullptr) {
        connect(inputDevice_, qOverload<int>(&QComboBox::activated), this,
                [this](int i) { c_->setInputDevice(inputDevice_->itemData(i).toString()); });
    }
    connect(latency_, qOverload<int>(&QComboBox::activated), this,
            [this](int i) { c_->setLatencyMode(latency_->itemData(i).toInt()); });
    if (sinkLayout_ != nullptr) {
        connect(sinkLayout_, qOverload<int>(&QComboBox::activated), this,
                [this](int i) { c_->setSinkLayout(sinkLayout_->itemData(i).toString()); });
    }
    if (inputLayout_ != nullptr) {
        connect(inputLayout_, qOverload<int>(&QComboBox::activated), this,
                [this](int i) { c_->setInputLayout(inputLayout_->itemData(i).toString()); });
    }

    latency_->setCurrentIndex(std::clamp(c_->latencyMode(), 0, 2));
    if (sinkLayout_ != nullptr) {
        const int i = sinkLayout_->findData(c_->sinkLayout());
        sinkLayout_->setCurrentIndex(i >= 0 ? i : 0);
    }
    if (inputLayout_ != nullptr) {
        const int i = inputLayout_->findData(c_->inputLayout());
        inputLayout_->setCurrentIndex(i >= 0 ? i : 0);
    }
    refreshDevices();
    return page;
}

// ---- updates -------------------------------------------------------------------------------

void MainWindow::fillDevices(QComboBox* combo, bool input, const QString& current) {
    const QSignalBlocker block(combo);
    combo->clear();
    QString defaultName;
    const QList<AppController::DeviceEntry> list = c_->devices(input);
    for (const auto& d : list) {
        if (d.isDefault) {
            defaultName = d.name;
        }
    }
    combo->addItem(defaultName.isEmpty() ? tr("Automatic") : tr("Automatic (%1)").arg(defaultName),
                   QString());
    for (const auto& d : list) {
        QString text = d.name;
        if (d.looksVirtual) {
            text += tr(" (virtual)");
        }
        combo->addItem(text, d.id);
    }
    int index = combo->findData(current);
    if (index < 0 && !current.isEmpty()) {
        // The chosen device is not there (unplugged): keep it visible so the choice is not lost.
        combo->addItem(tr("%1 (not connected)").arg(current), current);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(std::max(index, 0));
}

void MainWindow::refreshDevices() {
    fillDevices(outputDevice_, false, c_->outputDevice());
    if (inputDevice_ != nullptr) {
        fillDevices(inputDevice_, true, c_->inputDevice());
    }
}

void MainWindow::onParamsChanged() {
    const lsq::Params p = c_->params();
    updating_ = true;
    for (auto* map : {&rows_, &advancedRows_}) {
        for (auto& [key, r] : *map) {
            r->setValue(c_->paramValue(key));
        }
    }
    bypass_->setChecked(p.bypass);
    updating_ = false;
    curve_->setParams(p);
    curveSummary_->setText(curve_->summary());
    updateStatus(lastState_, lastText_);
}

void MainWindow::onPresetChanged() {
    updating_ = true;
    const int s = c_->strength();
    if (s >= 0) {
        strength_->setValue(s);
        strengthValue_->setText(tr("Strength: %1 %").arg(s));
    } else {
        strengthValue_->setText(tr("Strength: custom (you changed individual settings)"));
    }
    updating_ = false;
    refreshPresetCombo();
}

void MainWindow::refreshPresetCombo() {
    const QSignalBlocker block(preset_);
    preset_->clear();
    preset_->addItem(tr("Custom"), QString());
    for (std::size_t i = 0; i < lsq::kPresetCount; ++i) {
        const auto preset = static_cast<lsq::Preset>(i);
        preset_->addItem(QString::fromLatin1(lsq::presetName(preset)),
                         QString::fromLatin1(lsq::presetKey(preset)));
    }
    const QList<UserPreset> mine = c_->userPresets();
    for (const UserPreset& u : mine) {
        preset_->addItem(tr("%1 (mine)").arg(u.name), QStringLiteral("user:") + u.name);
    }
    const int index = preset_->findData(c_->presetKey());
    preset_->setCurrentIndex(index >= 0 ? index : 0);
    deletePreset_->setEnabled(c_->presetKey().startsWith(QStringLiteral("user:")));
}

void MainWindow::updateStatus(int state, const QString& text) {
    const bool processing = c_->processingEnabled();
    const IconState icon = iconStateFor(state, c_->bypass(), processing);
    QString line = statusSymbol(icon) + QLatin1Char(' ');
    if (!processing) {
        line += tr("Stopped. Sound sent to LiveSqueeze is not played.");
    } else if (c_->bypass() && icon == IconState::Bypassed) {
        line += tr("Bypassed: plain stereo downmix. %1").arg(text);
    } else {
        line += stateWords(state);
        if (!text.isEmpty()) {
            line += QStringLiteral(": ") + text;
        }
    }
    status_->setText(line);
    status_->setAccessibleDescription(line);
    processing_->setText(processing ? tr("&Processing is on") : tr("&Processing is off"));
    processing_->setAccessibleName(tr("Processing"));
}

void MainWindow::onState(int state, const QString& text) {
    lastState_ = state;
    lastText_ = text;
    updateStatus(state, text);
    refreshStats();
}

void MainWindow::onMeters(const lsqapp::MeterSnapshot& m) {
    inMeter_->setLevel(m.inPeakDb);
    outMeter_->setLevel(m.outPeakDb);
    gainMeter_->setGain(m.reductionDb, m.boostDb);
    curve_->setLiveLevel(m.levelDb);
}

void MainWindow::refreshStats() {
    const lsq::EngineStats s = c_->engineStats();
    QString text;
    if (s.running) {
        text = tr("Delay added: %1 ms. Dropouts: %2.").arg(s.latencyMs, 0, 'f', 0).arg(s.underruns);
    } else if (c_->state() == lsq::SupervisorState::Running) {
        text = tr("Waiting for sound to arrive. Dropouts so far: %1.").arg(s.underruns);
    }
    if (c_->state() == lsq::SupervisorState::Failed ||
        c_->state() == lsq::SupervisorState::Recovering) {
        const QString err = c_->lastError();
        if (!err.isEmpty() && err != lastText_) {
            text = err;
        }
    }
    if (stats_->text() != text) {
        stats_->setText(text);
    }
}

// ---- actions -------------------------------------------------------------------------------

void MainWindow::saveUserPreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name of the preset:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (!c_->saveUserPreset(name)) {
        QMessageBox::warning(this, tr("LiveSqueeze"), tr("The preset could not be saved."));
    }
}

void MainWindow::deleteUserPreset() {
    const QString key = c_->presetKey();
    if (!key.startsWith(QStringLiteral("user:"))) {
        return;
    }
    const QString name = key.mid(5);
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete the preset \"%1\"?").arg(name)) == QMessageBox::Yes) {
        c_->deleteUserPreset(name);
    }
}

void MainWindow::showDiagnostics() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Diagnostics"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(c_->diagnostics());
    text->setAccessibleName(tr("Diagnostics text"));
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* copy = buttons->addButton(tr("&Copy to clipboard"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(copy, &QPushButton::clicked, &dialog,
            [text] { QGuiApplication::clipboard()->setText(text->toPlainText()); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    const QFontMetrics fm = fontMetrics();
    dialog.resize(fm.horizontalAdvance(QStringLiteral("M")) * 70, fm.height() * 30);
    dialog.exec();
}

// ---- window events ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* e) {
    c_->saveNow();
    if (trayAvailable_) {
        hide();
        e->ignore();
    } else {
        e->accept();
        emit quitRequested();
    }
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    c_->setMetersVisible(true);
    statsTimer_.start();
    refreshStats();
}

void MainWindow::hideEvent(QHideEvent* e) {
    QMainWindow::hideEvent(e);
    c_->setMetersVisible(false);
    statsTimer_.stop();
}

} // namespace lsqapp
