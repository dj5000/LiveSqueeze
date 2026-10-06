#include <QAbstractButton>
#include <QAccessible>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTest>
#include <algorithm>

#include "MainWindow.hpp"
#include "app_test_util.hpp"
#include "doctest.h"

using namespace lsqapp;
using lsqtest::Fixture;

namespace {

// Everything a person can operate: if it is on screen it must be reachable and named.
bool isInteractive(const QWidget* w) {
    return qobject_cast<const QAbstractButton*>(w) != nullptr ||
           qobject_cast<const QSlider*>(w) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(w) != nullptr ||
           qobject_cast<const QComboBox*>(w) != nullptr ||
           qobject_cast<const QTabBar*>(w) != nullptr;
}

QString accessibleNameOf(QWidget* w) {
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(w);
    return iface != nullptr ? iface->text(QAccessible::Name) : QString();
}

QList<QWidget*> interactiveWidgets(QWidget* root) {
    QList<QWidget*> out;
    for (QWidget* w : root->findChildren<QWidget*>()) {
        // Parts inside a spin box or combo box are not separate controls.
        if (isInteractive(w) && w->parentWidget() != nullptr &&
            qobject_cast<QAbstractSpinBox*>(w->parentWidget()) == nullptr &&
            qobject_cast<QComboBox*>(w->parentWidget()) == nullptr &&
            w->objectName() != QLatin1String("qt_spinbox_lineedit")) {
            out.push_back(w);
        }
    }
    return out;
}

} // namespace

TEST_CASE("accessibility: every control has a name, on every page") {
    Fixture f(false);
    AppController c(f.options);
    MainWindow w(&c);
    w.show();
    CHECK(QTest::qWaitForWindowExposed(&w));

    int checked = 0;
    for (int tab = 0; tab < w.tabs()->count(); ++tab) {
        w.tabs()->setCurrentIndex(tab);
        for (QWidget* control : interactiveWidgets(&w)) {
            if (!control->isVisibleTo(&w)) {
                continue; // on another page
            }
            const QString name = accessibleNameOf(control);
            INFO(w.tabs()->tabText(tab).toStdString()
                 << ": " << control->metaObject()->className());
            CHECK_FALSE(name.trimmed().isEmpty());
            ++checked;
        }
    }
    CHECK(checked > 40); // the Advanced page alone has about 30 controls

    // The meters and the curve carry names and current values too.
    for (QWidget* m :
         {static_cast<QWidget*>(w.inputMeter()), static_cast<QWidget*>(w.outputMeter()),
          static_cast<QWidget*>(w.gainMeter()), static_cast<QWidget*>(w.curve())}) {
        CHECK_FALSE(m->accessibleName().isEmpty());
        CHECK_FALSE(m->accessibleDescription().isEmpty());
    }
    CHECK_FALSE(w.statusLabel()->accessibleName().isEmpty());
}

TEST_CASE("accessibility: sliders and number boxes explain themselves") {
    Fixture f(false);
    AppController c(f.options);
    MainWindow w(&c);
    w.show();
    w.tabs()->setCurrentIndex(1);
    int rows = 0;
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        ParamRow* r = w.advancedRow(QString::fromLatin1(d.key));
        if (std::string(d.key) == "bypass") {
            CHECK(r == nullptr); // lives next to the status line
            continue;
        }
        REQUIRE_MESSAGE(r != nullptr, d.key);
        ++rows;
        if (r->slider() != nullptr) {
            INFO(d.key);
            CHECK(r->slider()->accessibleName() == QString::fromLatin1(d.label));
            CHECK(r->slider()->accessibleDescription().contains(QStringLiteral(" to ")));
            CHECK(r->spin()->accessibleName().contains(QString::fromLatin1(d.label)));
        } else {
            CHECK(r->check()->accessibleName() == QString::fromLatin1(d.label));
        }
    }
    CHECK(rows == static_cast<int>(lsq::paramCount()) - 1);
}

TEST_CASE("accessibility: the keyboard reaches everything in a sensible order") {
    Fixture f(false);
    AppController c(f.options);
    MainWindow w(&c);
    w.show();
    w.activateWindow();
    CHECK(QTest::qWaitForWindowActive(&w));

    for (int tab = 0; tab < w.tabs()->count(); ++tab) {
        w.tabs()->setCurrentIndex(tab);
        QCoreApplication::processEvents();

        // Walk the focus chain with Tab and record what is visited.
        std::vector<QWidget*> visited;
        QWidget* start = QApplication::focusWidget();
        if (start == nullptr) {
            w.setFocus(Qt::TabFocusReason);
            start = QApplication::focusWidget();
        }
        REQUIRE(start != nullptr);
        for (int i = 0; i < 200; ++i) {
            visited.push_back(QApplication::focusWidget());
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            if (QApplication::focusWidget() == start) {
                break;
            }
        }
        INFO("page " << w.tabs()->tabText(tab).toStdString());
        CHECK(visited.size() < 150); // the chain is closed: Tab does not wander forever

        // Every control that is on screen on this page was visited ...
        for (QWidget* control : interactiveWidgets(&w)) {
            if (!control->isVisibleTo(&w) || !control->isEnabled() ||
                control->focusPolicy() == Qt::NoFocus) {
                continue;
            }
            INFO(control->metaObject()->className()
                 << " '" << accessibleNameOf(control).toStdString() << "'");
            CHECK(std::find(visited.begin(), visited.end(), control) != visited.end());
        }

        // ... and the page's own controls come top to bottom (left to right within a row; the
        // Simple page has two columns, so it may return to the top once).
        const int rowHeight = std::max(1, w.fontMetrics().height());
        int lastCentre = -1000000;
        int jumpsBack = 0;
        for (QWidget* v : visited) {
            if (v == nullptr || !isInteractive(v) || !v->isVisibleTo(&w) ||
                !w.tabs()->currentWidget()->isAncestorOf(v)) {
                continue;
            }
            const int centre = v->mapTo(&w, QPoint(0, v->height() / 2)).y();
            if (centre < lastCentre - rowHeight) {
                ++jumpsBack;
            }
            lastCentre = centre;
        }
        CHECK_MESSAGE(jumpsBack <= (w.tabs()->currentIndex() == 0 ? 1 : 0),
                      "the order jumps back up " << jumpsBack << " times on the page '"
                                                 << w.tabs()->tabText(tab).toStdString() << "'");
    }
}

TEST_CASE("accessibility: the main controls work from the keyboard") {
    Fixture f(false);
    AppController c(f.options);
    MainWindow w(&c);
    w.show();
    w.activateWindow();
    CHECK(QTest::qWaitForWindowActive(&w));

    // Ctrl+B (Command+B on a Mac) toggles bypass, Ctrl+P processing, Ctrl+1/2/3 the pages.
    CHECK_FALSE(c.bypass());
    QTest::keyClick(&w, Qt::Key_B, Qt::ControlModifier);
    CHECK(c.bypass());
    QTest::keyClick(&w, Qt::Key_B, Qt::ControlModifier);
    CHECK_FALSE(c.bypass());
    QTest::keyClick(&w, Qt::Key_2, Qt::ControlModifier);
    CHECK(w.tabs()->currentIndex() == 1);
    QTest::keyClick(&w, Qt::Key_3, Qt::ControlModifier);
    CHECK(w.tabs()->currentIndex() == 2);
    QTest::keyClick(&w, Qt::Key_1, Qt::ControlModifier);
    CHECK(w.tabs()->currentIndex() == 0);
    CHECK(c.processingEnabled());
    QTest::keyClick(&w, Qt::Key_P, Qt::ControlModifier);
    CHECK_FALSE(c.processingEnabled());
    QTest::keyClick(&w, Qt::Key_P, Qt::ControlModifier);
    CHECK(c.processingEnabled());

#if !defined(Q_OS_MACOS)
    // The underlined letters (Alt+B and so on) exist on Windows and Linux. A button reacts to its
    // mnemonic with a short animated click, so the event loop has to run for a moment.
    QTest::keyClick(&w, Qt::Key_B, Qt::AltModifier);
    CHECK(lsqtest::waitFor([&] { return c.bypass(); }, 1000));
    QTest::keyClick(&w, Qt::Key_B, Qt::AltModifier);
    CHECK(lsqtest::waitFor([&] { return !c.bypass(); }, 1000));
    QTest::keyClick(&w, Qt::Key_A, Qt::AltModifier);
    CHECK(w.tabs()->currentIndex() == 1);
    QTest::keyClick(&w, Qt::Key_D, Qt::AltModifier);
    CHECK(w.tabs()->currentIndex() == 2);
    QTest::keyClick(&w, Qt::Key_S, Qt::AltModifier);
    CHECK(w.tabs()->currentIndex() == 0);
#endif

    // Arrow keys move the strength slider; Page keys take larger steps.
    w.tabs()->setCurrentIndex(0);
    w.strengthSlider()->setFocus();
    const int before = w.strengthSlider()->value();
    QTest::keyClick(w.strengthSlider(), Qt::Key_Right);
    CHECK(w.strengthSlider()->value() == before + 1);
    QTest::keyClick(w.strengthSlider(), Qt::Key_PageUp);
    CHECK(w.strengthSlider()->value() == before + 11);
    CHECK(c.strength() == before + 11);

    // Number boxes accept typing.
    ParamRow* trim = w.row(QStringLiteral("out_trim_db"));
    trim->spin()->setFocus();
    trim->spin()->selectAll();
    QTest::keyClicks(trim->spin(), QStringLiteral("-12"));
    QTest::keyClick(trim->spin(), Qt::Key_Return);
    CHECK(c.paramValue(QStringLiteral("out_trim_db")) == doctest::Approx(-12.0));
}
