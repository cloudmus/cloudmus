#pragma once

class QIcon;

// The app's icon in the Dock (macOS only).
namespace Integration::DockIcon {

// Drawn from a large raster, so it's crisp at any size the Dock shows it,
// on Apple's icon grid so it matches the icons next to it.
void setIcon(const QIcon& logo);

// Hidden while the window sits in the tray: the app lives in the menu bar
// then, like other menu bar apps, and leaves Cmd+Tab as well.
void setVisible(bool visible);

} // namespace Integration::DockIcon
