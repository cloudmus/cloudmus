#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>

namespace Integration {

// Windows only: paints a new top-level window (a dialog, the main window)
// with the theme's own background the moment Windows asks it to erase
// itself. Before Qt's first frame DWM shows the window's surface unpainted,
// which is white — a flash, glaring with the dark theme. Hiding the window
// until it painted (cloaking) fixed that, but cost DWM's open and restore
// animations; this leaves the window to DWM and just makes that first
// glimpse dark. Installed on the application; popups aren't touched, and
// neither are glass windows (translucent, not erased).
class FirstFrameBackground : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
};

} // namespace Integration
