#include "Widgets.hpp"

#include <QAccessible>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <algorithm>
#include <cmath>

#include "lsq/db.hpp"
#include "lsq/gain_computer.hpp"

namespace lsqapp {

QString paramHelp(const QString& key) {
    struct Entry {
        const char* key;
        const char* text;
    };
    static const Entry kHelp[] = {
        {"center_gain_db", "Raises or lowers the center channel, where most dialogue is, in the "
                           "stereo mix."},
        {"surround_gain_db", "Raises or lowers the surround channels in the stereo mix."},
        {"lfe_gain_db", "Level of the subwoofer channel when it is included."},
        {"lfe_enabled", "Mix the subwoofer (low frequency effects) channel into the stereo "
                        "output."},
        {"normalize_downmix", "Scales the downmix so that sound that is the same in several "
                              "channels cannot go over full scale."},
        {"comp_enabled", "Turns the compressor on or off. The limiter stays on."},
        {"threshold_db", "Sounds louder than this are turned down."},
        {"ratio", "How strongly sounds above the threshold are turned down. At 4 to 1, 4 dB "
                  "above the threshold becomes 1 dB."},
        {"knee_db", "How gradually the compressor starts working around its thresholds."},
        {"upward_enabled", "Make quiet sounds, such as whispers, louder."},
        {"up_threshold_db", "Sounds quieter than this are made louder."},
        {"up_ratio", "How strongly quiet sounds are lifted. At 2 to 1, every 2 dB below the "
                     "threshold are lifted by 1 dB."},
        {"max_boost_db", "The most that quiet sounds are made louder."},
        {"noise_floor_db", "Sound at or below this level, such as hiss and room tone, is not "
                           "lifted."},
        {"attack_ms", "How quickly loud sounds are turned down."},
        {"release_ms", "How slowly the volume comes back up after a loud sound."},
        {"makeup_db", "Raises the volume of everything after compression."},
        {"sidechain_hpf", "Bass and rumble below 80 hertz do not make the compressor turn "
                          "things down."},
        {"link_max", "The compressor reacts to the louder of the left and right channels "
                     "instead of their average."},
        {"ceiling_db", "Nothing leaving LiveSqueeze is louder than this (true peak)."},
        {"limiter_release_ms", "How slowly the limiter lets go after catching a peak."},
        {"out_trim_db", "Volume of the processed sound. Turn it down here rather than with the "
                        "system volume."},
        {"bypass", "Plays the plain stereo downmix, without compression or limiting, for "
                   "comparison. Peaks are only clamped."},
    };
    for (const Entry& e : kHelp) {
        if (key == QLatin1String(e.key)) {
            return QString::fromLatin1(e.text);
        }
    }
    return {};
}

// ---- ParamRow --------------------------------------------------------------------------------

ParamRow::ParamRow(const lsq::ParamDesc& desc, QWidget* parent)
    : QWidget(parent), key_(QString::fromLatin1(desc.key)) {
    const QString label = QString::fromLatin1(desc.label);
    const QString unit = QString::fromLatin1(desc.unit);
    const QString help = paramHelp(key_);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (desc.kind == lsq::ParamKind::Bool) {
        check_ = new QCheckBox(label, this);
        check_->setToolTip(help);
        check_->setAccessibleName(label);
        check_->setAccessibleDescription(help);
        layout->addWidget(check_);
        connect(check_, &QCheckBox::toggled, this, [this](bool on) {
            if (!updating_) {
                emit valueEdited(key_, on ? 1.0 : 0.0);
            }
        });
        return;
    }

    min_ = static_cast<double>(desc.minValue);
    max_ = static_cast<double>(desc.maxValue);
    step_ = (max_ - min_) > 100.0 ? 1.0 : 0.1;
    const int steps = static_cast<int>(std::lround((max_ - min_) / step_));

    label_ = new QLabel(label, this);
    label_->setWordWrap(true);
    label_->setMinimumWidth(label_->fontMetrics().horizontalAdvance(QStringLiteral("M")) * 14);
    label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, steps);
    slider_->setPageStep(std::max(1, steps / 10));
    slider_->setMinimumWidth(slider_->fontMetrics().horizontalAdvance(QStringLiteral("M")) * 10);

    spin_ = new QDoubleSpinBox(this);
    spin_->setRange(min_, max_);
    spin_->setDecimals(step_ >= 1.0 ? 0 : 1);
    spin_->setSingleStep(step_);
    spin_->setKeyboardTracking(false);
    // The same width for every row, so the sliders of different rows line up.
    spin_->setMinimumWidth(spin_->fontMetrics().horizontalAdvance(QStringLiteral("-3000.0 dBTP")) +
                           spin_->fontMetrics().horizontalAdvance(QStringLiteral("M")) * 3);
    if (!unit.isEmpty()) {
        spin_->setSuffix(unit == QLatin1String(":1") ? QStringLiteral(" : 1")
                                                     : QStringLiteral(" ") + unit);
    }
    label_->setBuddy(spin_);

    const QString range = QObject::tr("%1 to %2%3")
                              .arg(min_, 0, 'f', step_ >= 1.0 ? 0 : 1)
                              .arg(max_, 0, 'f', step_ >= 1.0 ? 0 : 1)
                              .arg(unit.isEmpty() ? QString() : QStringLiteral(" ") + unit);
    const QString description =
        help.isEmpty() ? range : help + QLatin1Char(' ') + range + QLatin1Char('.');
    for (QWidget* w : {static_cast<QWidget*>(slider_), static_cast<QWidget*>(spin_)}) {
        w->setToolTip(help);
        w->setAccessibleDescription(description);
    }
    slider_->setAccessibleName(label);
    spin_->setAccessibleName(label + QStringLiteral(", number"));

    layout->addWidget(label_, 3);
    layout->addWidget(slider_, 4);
    layout->addWidget(spin_, 0);

    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        if (updating_) {
            return;
        }
        const double value = min_ + v * step_;
        setValue(value);
        emit valueEdited(key_, value);
    });
    connect(spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (updating_) {
            return;
        }
        setValue(v);
        emit valueEdited(key_, v);
    });
}

void ParamRow::setValue(double value) {
    updating_ = true;
    if (check_ != nullptr) {
        check_->setChecked(value >= 0.5);
    } else {
        spin_->setValue(value);
        slider_->setValue(static_cast<int>(std::lround((value - min_) / step_)));
    }
    updating_ = false;
}

double ParamRow::value() const {
    if (check_ != nullptr) {
        return check_->isChecked() ? 1.0 : 0.0;
    }
    return spin_->value();
}

// ---- meters ----------------------------------------------------------------------------------

namespace {

QString dbText(float db) {
    if (db <= -100.0f) {
        return QObject::tr("silent");
    }
    return QObject::tr("%1 dB").arg(static_cast<double>(db), 0, 'f', 1);
}

} // namespace

LevelMeter::LevelMeter(const QString& name, QWidget* parent) : QWidget(parent), name_(name) {
    setAccessibleName(name);
    setAccessibleDescription(valueText());
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lastUpdate_ = std::chrono::steady_clock::now();
    announce_.setInterval(1000);
    connect(&announce_, &QTimer::timeout, this, &LevelMeter::announce);
    announce_.start();
}

void LevelMeter::setLevel(float db) {
    // Jump up at once; fall back slowly (see displayedDb()).
    bar_ = std::max(db, displayedDb());
    lastUpdate_ = std::chrono::steady_clock::now();
    held_ = std::max(held_, db);
    update();
}

float LevelMeter::displayedDb() const {
    const float dt =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - lastUpdate_).count();
    return std::max(bar_ - 24.0f * dt, -120.0f);
}

void LevelMeter::announce() {
    shown_ = held_;
    held_ = -120.0f;
    const QString text = valueText();
    if (text != accessibleDescription()) {
        setAccessibleDescription(text);
    }
    update(); // the bar keeps falling when no readings arrive
}

QString LevelMeter::valueText() const {
    return dbText(shown_);
}

QSize LevelMeter::sizeHint() const {
    const QFontMetrics fm = fontMetrics();
    return {fm.horizontalAdvance(QStringLiteral("M")) * 36, fm.height() * 2 + 6};
}

QSize LevelMeter::minimumSizeHint() const {
    const QFontMetrics fm = fontMetrics();
    return {fm.horizontalAdvance(QStringLiteral("M")) * 20, fm.height() * 2 + 6};
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QFontMetrics fm = fontMetrics();
    const QString text = valueText();
    const int textW = fm.horizontalAdvance(QStringLiteral("-120.0 dB")) + 4;
    const QRect barArea(0, 0, width() - textW - 6, fm.height() + 2);
    const QPalette pal = palette();

    p.fillRect(barArea, pal.color(QPalette::Base));
    const float span = -kMinDb;
    const float frac = std::clamp((displayedDb() - kMinDb) / span, 0.0f, 1.0f);
    QRect fill = barArea.adjusted(1, 1, -1, -1);
    fill.setWidth(static_cast<int>(std::lround(fill.width() * static_cast<double>(frac))));
    p.fillRect(fill, pal.color(QPalette::Highlight));
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(barArea.adjusted(0, 0, -1, -1));

    // Scale: a tick and a number every 10 dB.
    p.setPen(pal.color(QPalette::WindowText));
    QFont small = font();
    small.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.8));
    p.setFont(small);
    const QFontMetrics sfm(small);
    for (int db = static_cast<int>(kMinDb); db <= 0; db += 10) {
        const int x = barArea.left() +
                      static_cast<int>(std::lround(barArea.width() * ((db - kMinDb) / span)));
        p.drawLine(x, barArea.bottom(), x, barArea.bottom() + 3);
        const QString label = QString::number(db);
        const int w = sfm.horizontalAdvance(label);
        p.drawText(std::clamp(x - w / 2, 0, barArea.width() - w),
                   barArea.bottom() + 3 + sfm.ascent(), label);
    }
    p.setFont(font());
    p.setPen(pal.color(QPalette::WindowText));
    p.drawText(QRect(barArea.right() + 6, 0, textW, barArea.height()),
               Qt::AlignRight | Qt::AlignVCenter, text);
}

GainMeter::GainMeter(const QString& name, QWidget* parent) : QWidget(parent), name_(name) {
    setAccessibleName(name);
    setAccessibleDescription(valueText());
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    announce_.setInterval(1000);
    connect(&announce_, &QTimer::timeout, this, &GainMeter::announce);
    announce_.start();
}

void GainMeter::setGain(float reductionDb, float boostDb) {
    reduction_ = reductionDb;
    boost_ = boostDb;
    heldReduction_ = std::min(heldReduction_, reductionDb);
    heldBoost_ = std::max(heldBoost_, boostDb);
    update();
}

void GainMeter::announce() {
    shownReduction_ = heldReduction_;
    shownBoost_ = heldBoost_;
    heldReduction_ = 0.0f;
    heldBoost_ = 0.0f;
    const QString text = valueText();
    if (text != accessibleDescription()) {
        setAccessibleDescription(text);
    }
}

QString GainMeter::valueText() const {
    return QObject::tr("turned down %1 dB, lifted %2 dB")
        .arg(static_cast<double>(-shownReduction_), 0, 'f', 1)
        .arg(static_cast<double>(shownBoost_), 0, 'f', 1);
}

QSize GainMeter::sizeHint() const {
    const QFontMetrics fm = fontMetrics();
    return {fm.horizontalAdvance(QStringLiteral("M")) * 36, fm.height() * 2 + 6};
}

QSize GainMeter::minimumSizeHint() const {
    const QFontMetrics fm = fontMetrics();
    return {fm.horizontalAdvance(QStringLiteral("M")) * 20, fm.height() * 2 + 6};
}

void GainMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QFontMetrics fm = fontMetrics();
    const QPalette pal = palette();
    const QString text = QObject::tr("-%1 / +%2 dB")
                             .arg(static_cast<double>(-reduction_), 0, 'f', 1)
                             .arg(static_cast<double>(boost_), 0, 'f', 1);
    const int textW = fm.horizontalAdvance(QStringLiteral("-24.0 / +12.0 dB")) + 4;
    const QRect area(0, 0, width() - textW - 6, fm.height() + 2);
    p.fillRect(area, pal.color(QPalette::Base));

    const double total = static_cast<double>(kMaxReductionDb + kMaxBoostDb);
    const int zeroX =
        area.left() + static_cast<int>(std::lround(area.width() *
                                                   (static_cast<double>(kMaxReductionDb) / total)));
    const int leftW = static_cast<int>(
        std::lround(area.width() *
                    (static_cast<double>(std::clamp(-reduction_, 0.0f, kMaxReductionDb)) / total)));
    const int rightW = static_cast<int>(std::lround(
        area.width() * (static_cast<double>(std::clamp(boost_, 0.0f, kMaxBoostDb)) / total)));
    // Down is a solid bar to the left of zero, up a hatched bar to the right: told apart by
    // position and texture as well as by the numbers.
    p.fillRect(QRect(zeroX - leftW, area.top() + 1, leftW, area.height() - 2),
               pal.color(QPalette::Highlight));
    p.fillRect(QRect(zeroX, area.top() + 1, rightW, area.height() - 2),
               QBrush(pal.color(QPalette::Highlight), Qt::BDiagPattern));
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(area.adjusted(0, 0, -1, -1));
    p.setPen(pal.color(QPalette::WindowText));
    p.drawLine(zeroX, area.top(), zeroX, area.bottom() + 3);

    QFont small = font();
    small.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.8));
    p.setFont(small);
    const QFontMetrics sfm(small);
    for (const int db : {-24, -12, 0, 6, 12}) {
        const int x = zeroX + static_cast<int>(std::lround(area.width() * (db / total)));
        p.drawLine(x, area.bottom(), x, area.bottom() + 3);
        const QString label = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
        const int w = sfm.horizontalAdvance(label);
        p.drawText(std::clamp(x - w / 2, 0, area.width() - w), area.bottom() + 3 + sfm.ascent(),
                   label);
    }
    p.setFont(font());
    p.drawText(QRect(area.right() + 6, 0, textW, area.height()), Qt::AlignRight | Qt::AlignVCenter,
               text);
}

// ---- CurveView -------------------------------------------------------------------------------

CurveView::CurveView(QWidget* parent) : QWidget(parent) {
    setAccessibleName(tr("Compression curve"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setParams(params_);
}

void CurveView::setParams(const lsq::Params& p) {
    params_ = p;
    setAccessibleDescription(summary());
    update();
}

void CurveView::setLiveLevel(float detectorDb) {
    live_ = detectorDb;
    update();
}

QString CurveView::summary() const {
    if (params_.bypass) {
        return tr("Bypass is on: the sound is only downmixed to stereo.");
    }
    if (!params_.compEnabled) {
        return tr("The compressor is off. The limiter keeps peaks below %1 dB.")
            .arg(static_cast<double>(params_.ceilingDb), 0, 'f', 1);
    }
    QString text = tr("Sounds louder than %1 dB are turned down at %2 to 1.")
                       .arg(static_cast<double>(params_.thresholdDb), 0, 'f', 0)
                       .arg(static_cast<double>(params_.ratio), 0, 'f', 1);
    if (params_.upwardEnabled) {
        text += tr(" Sounds quieter than %1 dB are lifted at %2 to 1, by at most %3 dB.")
                    .arg(static_cast<double>(params_.upThresholdDb), 0, 'f', 0)
                    .arg(static_cast<double>(params_.upRatio), 0, 'f', 1)
                    .arg(static_cast<double>(params_.maxBoostDb), 0, 'f', 0);
    }
    text += tr(" The limiter keeps peaks below %1 dB.")
                .arg(static_cast<double>(params_.ceilingDb), 0, 'f', 1);
    return text;
}

QSize CurveView::sizeHint() const {
    const int s = fontMetrics().horizontalAdvance(QStringLiteral("M")) * 24;
    return {s, s};
}

QSize CurveView::minimumSizeHint() const {
    const int s = fontMetrics().horizontalAdvance(QStringLiteral("M")) * 16;
    return {s, s};
}

void CurveView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    const QFontMetrics fm = fontMetrics();

    constexpr float kLo = -70.0f;
    constexpr float kHi = 0.0f;
    const int margin = fm.height() + fm.horizontalAdvance(QStringLiteral("-70")) + 10;
    const int bottomMargin = fm.height() * 2 + 6;
    const QRect plot(margin, 6, std::max(10, width() - margin - 8),
                     std::max(10, height() - bottomMargin - 6));

    auto mapX = [&](float db) -> double {
        return plot.left() + static_cast<double>((db - kLo) / (kHi - kLo)) * plot.width();
    };
    auto mapY = [&](float db) -> double {
        return plot.bottom() -
               static_cast<double>((std::clamp(db, kLo, kHi) - kLo) / (kHi - kLo)) * plot.height();
    };

    p.fillRect(plot, pal.color(QPalette::Base));
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    for (int db = -60; db <= 0; db += 10) {
        p.drawLine(QPointF(mapX(static_cast<float>(db)), plot.top()),
                   QPointF(mapX(static_cast<float>(db)), plot.bottom()));
        p.drawLine(QPointF(plot.left(), mapY(static_cast<float>(db))),
                   QPointF(plot.right(), mapY(static_cast<float>(db))));
    }
    p.setPen(pal.color(QPalette::WindowText));
    for (int db = -60; db <= 0; db += 20) {
        const QString label = QString::number(db);
        p.drawText(QPointF(mapX(static_cast<float>(db)) - fm.horizontalAdvance(label) / 2.0,
                           plot.bottom() + fm.ascent() + 3),
                   label);
        p.drawText(QPointF(plot.left() - fm.horizontalAdvance(label) - 4,
                           mapY(static_cast<float>(db)) + fm.ascent() / 2.0),
                   label);
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + fm.height() + 3, plot.width(), fm.height()),
               Qt::AlignCenter, tr("Input level (dB)"));

    // The line where output equals input (no change), dashed.
    QPen dashed(pal.color(QPalette::Mid), 1, Qt::DashLine);
    p.setPen(dashed);
    p.drawLine(QPointF(mapX(kLo), mapY(kLo)), QPointF(mapX(kHi), mapY(kHi)));

    // The limiter ceiling, dotted, with its label.
    if (!params_.bypass) {
        QPen dotted(pal.color(QPalette::WindowText), 1, Qt::DotLine);
        p.setPen(dotted);
        p.drawLine(QPointF(plot.left(), mapY(params_.ceilingDb)),
                   QPointF(plot.right(), mapY(params_.ceilingDb)));
    }

    // The curve itself.
    QPainterPath path;
    bool first = true;
    for (float x = kLo; x <= kHi + 0.01f; x += 0.5f) {
        const float gain = params_.bypass ? 0.0f : lsq::curve::staticGainDb(x, params_);
        float y = x + gain;
        if (!params_.bypass) {
            y = std::min(y, params_.ceilingDb);
        }
        const QPointF pt(mapX(x), mapY(y));
        if (first) {
            path.moveTo(pt);
            first = false;
        } else {
            path.lineTo(pt);
        }
    }
    QPen curvePen(pal.color(QPalette::Highlight), std::max(2.0, fm.height() / 7.0));
    if (params_.bypass) {
        curvePen.setStyle(Qt::DashDotLine);
    }
    p.setPen(curvePen);
    p.drawPath(path);

    // Where the sound is right now.
    if (live_ > kLo) {
        const float gain = params_.bypass ? 0.0f : lsq::curve::staticGainDb(live_, params_);
        const QPointF c(mapX(live_), mapY(std::min(live_ + gain, params_.ceilingDb)));
        const double r = std::max(4.0, fm.height() / 3.0);
        p.setPen(QPen(pal.color(QPalette::WindowText), 2));
        p.setBrush(pal.color(QPalette::Base));
        p.drawEllipse(c, r, r);
    }
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot.adjusted(0, 0, -1, -1));

    // Vertical axis title, rotated.
    p.setPen(pal.color(QPalette::WindowText));
    p.save();
    p.translate(2, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-plot.height() / 2.0, -2, plot.height(), fm.height()), Qt::AlignCenter,
               tr("Output (dB)"));
    p.restore();
}

} // namespace lsqapp
