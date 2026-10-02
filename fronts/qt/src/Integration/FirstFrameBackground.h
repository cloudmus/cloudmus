#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QSet>

namespace Integration {

// Windows only: paints a new top-level window (a dialog, the main window)
// with the theme's own background the moment Windows asks it to erase
// itself. Before Qt's first frame DWM shows the window's surface unpainted,
// which is white — a flash, glaring with the dark theme. Hiding the window
// until it painted (cloaking) fixed that, but cost DWM's open and restore
// animations; this leaves the window to DWM and just makes that first
// glimpse the theme's color. Only until the window's first paint after
// each show: later erases (a move, a resize) are left to Qt, which skips
// them — filled, the window flickered while dragged. Installed on the
// application; popups aren't touched, and neither are translucent windows
// (the main window with glass), which aren't erased.
class FirstFrameBackground : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    // HWNDs that have painted since they were last shown.
    QSet<void*> painted_;
};

} // namespace Integration
