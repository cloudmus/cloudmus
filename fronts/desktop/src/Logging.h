#pragma once

// Debug logging toggle, mirroring fronts/tui/cloudmus_tui/log.py's
// CLOUDMUS_TUI_DEBUG convention: verbose qDebug()/qInfo() output is
// suppressed by default (only warnings/errors reach stderr) so normal runs
// stay quiet, and enabling it also mirrors everything to a log file since a
// GUI app's stderr is often not visible (e.g. launched from a .desktop
// file). Call installLogging() once, right after constructing QApplication
// (debugLoggingRequested() reads QCoreApplication::arguments(), so it needs
// the app object to already exist).

bool debugLoggingRequested();
void installLogging();
