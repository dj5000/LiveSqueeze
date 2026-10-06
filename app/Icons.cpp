#include "Icons.hpp"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include "lsq/supervisor.hpp"

namespace lsqapp {

IconState iconStateFor(int supervisorState, bool bypass, bool processingEnabled) {
    if (!processingEnabled) {
        return IconState::Stopped;
    }
    switch (static_cast<lsq::SupervisorState>(supervisorState)) {
    case lsq::SupervisorState::Running:
        return bypass ? IconState::Bypassed : IconState::Running;
    case lsq::SupervisorState::Starting:
        return IconState::Starting;
    case lsq::SupervisorState::Recovering:
    case lsq::SupervisorState::Failed:
        return IconState::Problem;
    case lsq::SupervisorState::Idle:
        break;
    }
    return IconState::Stopped;
}

QString statusSymbol(IconState s) {
    switch (s) {
    case IconState::Running:
        return QStringLiteral("●"); // ●
    case IconState::Bypassed:
        return QStringLiteral("○"); // ○
    case IconState::Problem:
        return QStringLiteral("▲"); // ▲
    case IconState::Stopped:
        return QStringLiteral("■"); // ■
    case IconState::Starting:
        return QStringLiteral("◆"); // ◆
    }
    return {};
}

namespace {

// A squeezed waveform glyph: loud bars pressed down, quiet bars lifted, in the middle of the shape.
void drawGlyph(QPainter& p, const QRectF& area, const QColor& color) {
    const double w = area.width() / 9.0;
    static const double kHeights[] = {0.30, 0.55, 0.80, 0.55, 0.30};
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int i = 0; i < 5; ++i) {
        const double h = area.height() * kHeights[i];
        p.drawRoundedRect(QRectF(area.left() + w * (2 * i), area.center().y() - h / 2, w, h), w / 2,
                          w / 2);
    }
}

QPixmap render(IconState s, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double m = size * 0.06;
    const QRectF box(m, m, size - 2 * m, size - 2 * m);
    const QColor dark(0x20, 0x20, 0x28);
    const QColor light(0xF8, 0xF8, 0xFA);
    const double outline = std::max(1.0, size / 16.0);

    QColor fill;
    switch (s) {
    case IconState::Running:
        fill = QColor(0x1E, 0x9E, 0x5A);
        break;
    case IconState::Bypassed:
        fill = QColor(0x3A, 0x6E, 0xD8);
        break;
    case IconState::Problem:
        fill = QColor(0xF2, 0xA9, 0x00);
        break;
    case IconState::Stopped:
        fill = QColor(0x80, 0x84, 0x8C);
        break;
    case IconState::Starting:
        fill = QColor(0x8E, 0x5B, 0xC8);
        break;
    }

    QPainterPath shape;
    switch (s) {
    case IconState::Running:
        shape.addEllipse(box);
        break;
    case IconState::Bypassed:
        shape.addEllipse(box);
        break;
    case IconState::Problem:
        shape.addPolygon(
            QPolygonF({QPointF(box.center().x(), box.top()), QPointF(box.right(), box.bottom()),
                       QPointF(box.left(), box.bottom())}));
        shape.closeSubpath();
        break;
    case IconState::Stopped:
        shape.addRoundedRect(box.adjusted(size * 0.04, size * 0.04, -size * 0.04, -size * 0.04),
                             size * 0.08, size * 0.08);
        break;
    case IconState::Starting:
        shape.addPolygon(QPolygonF(
            {QPointF(box.center().x(), box.top()), QPointF(box.right(), box.center().y()),
             QPointF(box.center().x(), box.bottom()), QPointF(box.left(), box.center().y())}));
        shape.closeSubpath();
        break;
    }

    p.setPen(QPen(dark, outline * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(s == IconState::Bypassed ? light : fill);
    p.drawPath(shape);

    if (s == IconState::Bypassed) {
        // A ring: the inside stays light, the glyph is drawn in the fill colour.
        drawGlyph(p, box.adjusted(size * 0.2, size * 0.2, -size * 0.2, -size * 0.2), fill);
    } else if (s == IconState::Problem) {
        // An exclamation mark.
        p.setPen(Qt::NoPen);
        p.setBrush(dark);
        const double cx = box.center().x();
        p.drawRoundedRect(
            QRectF(cx - size * 0.05, box.top() + size * 0.36, size * 0.10, size * 0.28),
            size * 0.04, size * 0.04);
        p.drawEllipse(QPointF(cx, box.bottom() - size * 0.14), size * 0.06, size * 0.06);
    } else if (s == IconState::Stopped) {
        // Two pause bars.
        p.setPen(Qt::NoPen);
        p.setBrush(light);
        p.drawRect(QRectF(size * 0.34, size * 0.30, size * 0.12, size * 0.40));
        p.drawRect(QRectF(size * 0.54, size * 0.30, size * 0.12, size * 0.40));
    } else if (s == IconState::Starting) {
        p.setPen(Qt::NoPen);
        p.setBrush(light);
        for (int i = 0; i < 3; ++i) {
            p.drawEllipse(QPointF(size * (0.32 + 0.18 * i), size * 0.5), size * 0.06, size * 0.06);
        }
    } else {
        drawGlyph(p, box.adjusted(size * 0.2, size * 0.2, -size * 0.2, -size * 0.2), light);
    }
    return pm;
}

} // namespace

QIcon makeTrayIcon(IconState s) {
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48, 64}) {
        icon.addPixmap(render(s, size));
    }
    return icon;
}

QIcon makeAppIcon() {
    return makeTrayIcon(IconState::Running);
}

} // namespace lsqapp
