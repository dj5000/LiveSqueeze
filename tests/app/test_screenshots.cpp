// Draws the window in the situations that matter for people who need large text, high contrast or
// a dark theme, saves pictures of it (see LSQ_SCREENSHOT_DIR) and checks that nothing is cut off.
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QStyleFactory>
#include <QTest>
#include <set>

#include "MainWindow.hpp"
#include "app_test_util.hpp"
#include "doctest.h"

using namespace lsqapp;
using lsqtest::Fixture;

namespace {

QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2B, 0x2B, 0x30));
    p.setColor(QPalette::WindowText, QColor(0xEE, 0xEE, 0xF0));
    p.setColor(QPalette::Base, QColor(0x1B, 0x1B, 0x1F));
    p.setColor(QPalette::AlternateBase, QColor(0x33, 0x33, 0x38));
    p.setColor(QPalette::Text, QColor(0xEE, 0xEE, 0xF0));
    p.setColor(QPalette::Button, QColor(0x3A, 0x3A, 0x42));
    p.setColor(QPalette::ButtonText, QColor(0xEE, 0xEE, 0xF0));
    p.setColor(QPalette::Mid, QColor(0x70, 0x70, 0x78));
    p.setColor(QPalette::Highlight, QColor(0x4F, 0x9D, 0xFF));
    p.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x14));
    p.setColor(QPalette::ToolTipBase, QColor(0x3A, 0x3A, 0x42));
    p.setColor(QPalette::ToolTipText, QColor(0xEE, 0xEE, 0xF0));
    return p;
}

// What a screen shows when text is "scaled": a larger default font.
struct Appearance {
    const char* name;
    double fontScale;
    bool dark;
};

class ScopedAppearance {
public:
    explicit ScopedAppearance(const Appearance& a)
        : oldFont_(QApplication::font()), oldPalette_(QApplication::palette()) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QFont f = oldFont_;
        f.setPointSizeF(oldFont_.pointSizeF() * a.fontScale);
        QApplication::setFont(f);
        QApplication::setPalette(a.dark ? darkPalette() : QApplication::style()->standardPalette());
    }
    ~ScopedAppearance() {
        QApplication::setFont(oldFont_);
        QApplication::setPalette(oldPalette_);
    }

private:
    QFont oldFont_;
    QPalette oldPalette_;
};

// Reports the first widget whose text does not fit, or an empty string.
QString findClipping(QWidget* root) {
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (!w->isVisibleTo(root)) {
            continue;
        }
        QString text;
        bool wraps = false;
        if (auto* l = qobject_cast<QLabel*>(w)) {
            text = l->text();
            wraps = l->wordWrap();
        } else if (auto* b = qobject_cast<QAbstractButton*>(w)) {
            text = b->text();
        }
        text.remove(QLatin1Char('&'));
        if (text.isEmpty() || text.contains(QLatin1Char('<'))) {
            continue;
        }
        const QFontMetrics fm = w->fontMetrics();
        if (wraps) {
            // Wrapped text must have the room its lines need.
            const QRect need =
                fm.boundingRect(QRect(0, 0, w->width(), 100000), Qt::TextWordWrap, text);
            if (need.height() > w->height() + 2) {
                return QStringLiteral("wrapped text cut off (height): '%1'").arg(text);
            }
        } else if (qobject_cast<QPushButton*>(w) == nullptr &&
                   qobject_cast<QCheckBox*>(w) == nullptr) {
            if (fm.horizontalAdvance(text) > w->width() + 2) {
                return QStringLiteral("text cut off (width): '%1' in %2 px")
                    .arg(text)
                    .arg(w->width());
            }
        } else if (fm.horizontalAdvance(text) > w->width()) {
            return QStringLiteral("button text cut off: '%1' in %2 px").arg(text).arg(w->width());
        }
    }
    return {};
}

// A picture with real content: more than a few distinct colours, and not a single flat area.
bool looksDrawn(const QImage& img) {
    std::set<QRgb> colours;
    for (int y = 0; y < img.height(); y += 3) {
        for (int x = 0; x < img.width(); x += 3) {
            colours.insert(img.pixel(x, y));
            if (colours.size() > 12) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("screenshots: every page in light and dark, at normal and double text size") {
    const Appearance kAppearances[] = {
        {"light-100", 1.0, false},
        {"dark-100", 1.0, true},
        {"light-200", 2.0, false},
        {"dark-200", 2.0, true},
    };
    const char* kPages[] = {"simple", "advanced", "devices"};
    QDir().mkpath(QStringLiteral(LSQ_SCREENSHOT_DIR));

    for (const Appearance& a : kAppearances) {
        ScopedAppearance scoped(a);
        Fixture f;
        AppController c(f.options);
        MainWindow w(&c);
        w.show();
        CHECK(QTest::qWaitForWindowExposed(&w));
        lsqtest::waitFor([&] { return c.state() == lsq::SupervisorState::Running; });
        // Let the meters show something.
        lsqtest::waitFor([&] { return w.inputMeter()->displayedDb() > -40.0f; }, 3000);
        w.inputMeter()->announce();

        for (int page = 0; page < 3; ++page) {
            w.tabs()->setCurrentIndex(page);
            QCoreApplication::processEvents();
            QTest::qWait(50);
            const QImage img = w.grab().toImage();
            const QString file =
                QStringLiteral(LSQ_SCREENSHOT_DIR "/%1-%2.png")
                    .arg(QString::fromLatin1(kPages[page]), QString::fromLatin1(a.name));
            CHECK(img.save(file));
            INFO(a.name << " " << kPages[page]);
            CHECK(looksDrawn(img));
            CHECK(img.width() > 300);

            // No text may be cut off, and the window must not demand more than a screen.
            const QString clipped = findClipping(&w);
            CHECK_MESSAGE(clipped.isEmpty(), clipped.toStdString());
        }
        // The window is as wide as its content needs, within a 1920 pixel screen at 100% and a
        // 3840 pixel one at 200%.
        const int limit = a.fontScale > 1.5 ? 3840 : 1920;
        CHECK(w.minimumSizeHint().width() <= limit);
    }
}
