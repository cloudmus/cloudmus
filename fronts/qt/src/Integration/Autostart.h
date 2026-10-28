#pragma once

#include <QString>

namespace Integration {

// "Launch at login" via the XDG autostart spec: a .desktop entry in
// $XDG_CONFIG_HOME/autostart/, which every freedesktop session (Plasma,
// GNOME, XFCE, ...) runs at login. The entry starts the thing that started
// this process — the AppImage when running from one ($APPIMAGE; the path
// inside it is a temporary mount), otherwise the launcher ($CLOUDMUS_LAUNCHER;
// the Nix package's bin/cloudmus, which seeds the backends) — with
// kLaunchedAtLoginArgument, so main() can tell a login launch from one the
// user started by hand.
namespace Autostart {

inline constexpr auto kLaunchedAtLoginArgument = "--autostart";

// The entry exists and isn't switched off (Hidden=true, or GNOME's
// X-GNOME-Autostart-enabled=false — a session's own autostart settings
// may flip those instead of deleting the file).
bool isEnabled();
// Writes or removes the entry; false (with a warning logged) on failure.
bool setEnabled(bool enabled);
// Re-points an enabled entry at this binary if the one it names is gone (an
// AppImage moved or removed, a Nix package upgraded and its old store path
// collected), or if this is another AppImage or launcher than the one it names
// — a newer download kept next to the old one. Call once at startup.
void refresh();

} // namespace Autostart

} // namespace Integration
