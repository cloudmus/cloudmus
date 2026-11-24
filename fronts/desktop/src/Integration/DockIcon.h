#pragma once

#include <functional>

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

// Called on a click on the Dock icon, or when the app is opened again while
// it runs (Finder, Spotlight, a login item): macOS sends the running app a
// reopen event then, and expects its window back — even one in the tray.
void setReopenHandler(std::function<void()> handler);

} // namespace Integration::DockIcon
