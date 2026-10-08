#pragma once

#include <QString>

// Where the Qt front keeps what is not configuration. config.ini and the
// like stay under the config directory (see Settings); logs and crash
// reports are state — they must survive a restart, but nobody should back
// them up or sync them — and live in the XDG state directory.
namespace Config {

// <state home>/cloudmus/fronts/qt: debug.log and crashes/. The state home is
// $XDG_STATE_HOME or ~/.local/state; on Windows, which has no such thing,
// it is %LOCALAPPDATA%, where these files always were.
QString stateDir();

// The state home itself, from the two values it depends on (the
// environment's XDG_STATE_HOME, which counts only when absolute, and the
// home directory). Split out so tests don't need to touch the environment.
QString stateHomeFor(const QString& xdgStateHome, const QString& homeDir);

// Earlier versions kept debug.log, crashes/ (with the crash service's unsent
// events) and the AppImage's asan.log.* under the config directory. Moves
// them to the state directory; whatever is already there stays, and nothing
// is deleted (an emptied old directory aside). Run before anything opens
// them, so before logging starts.
void migrateLegacyState();
void migrateLegacyState(const QString& configDir, const QString& stateDir);

} // namespace Config
