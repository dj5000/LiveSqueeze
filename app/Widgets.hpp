#pragma once

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QWidget>
#include <chrono>

#include "lsq/params.hpp"

namespace lsqapp {

// A short explanation of a parameter, for tooltips and screen readers.
QString paramHelp(const QString& key);

// One row of the advanced view, built from a ParamDesc: a label, a slider and a number box for
// numbers; a check box for on/off settings. Everything is reachable with the keyboard, and the
// label wraps instead of being cut off when the text is large.
class ParamRow : public QWidget {
    Q_OBJECT
public:
    explicit ParamRow(const lsq::ParamDesc& desc, QWidget* parent = nullptr);

    QString key() const { return key_; }
    // Sets the shown value without emitting valueEdited().
    void setValue(double value);
    double value() const;

    QSlider* slider() const { return slider_; }
    QDoubleSpinBox* spin() const { return spin_; }
    QCheckBox* check() const { return check_; }

signals:
    void valueEdited(const QString& key, double value);

private:
    QString key_;
    QLabel* label_ = nullptr;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* spin_ = nullptr;
    QCheckBox* check_ = nullptr;
    double min_ = 0.0;
    double max_ = 1.0;
    double step_ = 0.1;
    bool updating_ = false;
};

// A horizontal level meter in dB. The number is always shown next to the bar, so nothing is
// conveyed by colour alone. The bar jumps up at once and falls back slowly; the number is the
// highest value of the last second. The accessible description follows the number about once a
// second, so a screen reader is not flooded.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(const QString& name, QWidget* parent = nullptr);

    void setLevel(float db);   // a new peak reading
    float displayedDb() const; // what the bar shows now: falls back 24 dB per second
    QString valueText() const;
    // Publishes the highest reading of the last second as the number and the accessible
    // description. Called by a once-a-second timer; public so tests need not wait.
    void announce();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    static constexpr float kMinDb = -60.0f;
    float bar_ = -120.0f;   // what the bar shows
    float held_ = -120.0f;  // highest reading in the current second
    float shown_ = -120.0f; // the number
    std::chrono::steady_clock::time_point lastUpdate_;
    QTimer announce_;
    QString name_;
};

// How far the compressor and limiter turned things down (to the left of the middle) and how far
// it lifted quiet things (to the right).
class GainMeter : public QWidget {
    Q_OBJECT
public:
    explicit GainMeter(const QString& name, QWidget* parent = nullptr);

    void setGain(float reductionDb, float boostDb); // reduction <= 0, boost >= 0
    QString valueText() const;
    void announce(); // see LevelMeter::announce()

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    static constexpr float kMaxReductionDb = 24.0f;
    static constexpr float kMaxBoostDb = 12.0f;
    float reduction_ = 0.0f;
    float boost_ = 0.0f;
    float heldReduction_ = 0.0f;
    float heldBoost_ = 0.0f;
    float shownReduction_ = 0.0f;
    float shownBoost_ = 0.0f;
    QTimer announce_;
    QString name_;
};

// The compression curve: output level against input level, with the limiter ceiling and a dot at
// the level currently being measured.
class CurveView : public QWidget {
    Q_OBJECT
public:
    explicit CurveView(QWidget* parent = nullptr);

    void setParams(const lsq::Params& p);
    void setLiveLevel(float detectorDb);
    QString summary() const; // the same information in words

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    lsq::Params params_;
    float live_ = -120.0f;
};

} // namespace lsqapp
