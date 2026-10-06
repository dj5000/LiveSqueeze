#pragma once

#include <QIcon>
#include <QString>

namespace lsqapp {

// What the tray icon (and the status line) says. Each state has its own outline, so it can be
// told apart without colour: circle = processing, ring = bypassed, triangle = problem,
// square = stopped, diamond = starting.
enum class IconState { Running, Bypassed, Problem, Stopped, Starting };

IconState iconStateFor(int supervisorState, bool bypass, bool processingEnabled);

// A symbol in front of the status text, matching the icon's shape.
QString statusSymbol(IconState s);

QIcon makeTrayIcon(IconState s);
QIcon makeAppIcon();

} // namespace lsqapp
