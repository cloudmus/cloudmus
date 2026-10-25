#pragma once

#include <QString>

namespace Integration {

// "Launch at login" via the XDG autostart spec: a .desktop entry in
// $XDG_CONFIG_HOME/autostart/, which every freedesktop session (Plasma,
// GNOME, XFCE, ...) runs at login. The entry starts this very binary —
// the AppImage when running from one ($APPIMAGE; the path inside it is a
// temporary mount) — with kLaunchedAtLoginArgument, so main() can tell a
// login launch from one the user started by hand. On Windows it is the
// HKCU Run key (AutostartWin.cpp), on macOS a launchd agent
// (AutostartMac.mm) — each with the same argument.
namespace Autostart {

inline constexpr auto kLaunchedAtLoginArgument = "--autostart";

// The entry exists and isn't switched off (Hidden=true, or GNOME's
// X-GNOME-Autostart-enabled=false — a session's own autostart settings
// may flip those instead of deleting the file).
bool isEnabled();
// Writes or removes the entry; false (with a warning logged) on failure.
bool setEnabled(bool enabled);
// Re-points an enabled entry at this binary if the one it names is gone
// (an AppImage moved or renamed), or if this is another AppImage than the
// one it names — a newer download kept next to the old one. Call once at
// startup.
void refresh();

} // namespace Autostart

} // namespace Integration
