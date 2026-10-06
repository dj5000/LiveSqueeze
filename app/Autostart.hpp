#pragma once

#include <QString>

namespace lsqapp::autostart {

// Start LiveSqueeze when the user logs in: a .desktop file on Linux, a Run registry value on
// Windows, a LaunchAgent on macOS. The program is started with --minimized.
bool isEnabled();
bool setEnabled(bool enabled, const QString& executablePath);

// Where the entry lives (the registry key path on Windows), for tests and diagnostics.
QString entryLocation();

} // namespace lsqapp::autostart
