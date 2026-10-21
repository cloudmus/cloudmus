#pragma once

class QIcon;
class QPixmap;

// The app's icon in the Dock (macOS only).
namespace Integration::DockIcon {

// Drawn from a large raster, so it's crisp at any size the Dock shows it,
// on Apple's icon grid so it matches the icons next to it.
void setIcon(const QIcon& logo);

// The playing track's cover in place of the logo, on the same grid with
// the logo in its corner (Integration::DockTile); a null one brings the
// logo back.
void setCover(const QPixmap& cover);

// Hidden while the window sits in the tray: the app lives in the menu bar
// then, like other menu bar apps, and leaves Cmd+Tab as well.
void setVisible(bool visible);

} // namespace Integration::DockIcon
