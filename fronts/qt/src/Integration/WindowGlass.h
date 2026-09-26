#pragma once

#include <QRegion>

class QWidget;

namespace Integration::WindowGlass {

// Asks the window system to blur what's behind a translucent top-level
// window — the "glass" the app's own tinted, partly transparent chrome is
// painted over (Theme::glassEnabled()). Platform-specific underneath:
//   - Linux/KDE (KWin, X11 and Wayland): KWindowEffects::enableBlurBehind(),
//     from KF6WindowSystem, loaded at runtime rather than linked — no build
//     dependency, and a build without KDE libraries (the AppImage on another
//     desktop) just has no glass.
//   - GNOME/Mutter and other compositors: no way for a client to ask for a
//     blur, so unavailable — a translucent window there would show the
//     desktop through it unblurred.
//   - Windows / macOS: not yet — the place for DwmSetWindowAttribute(
//     DWMWA_SYSTEMBACKDROP_TYPE, acrylic) / an NSVisualEffectView behind the
//     window's content.

// Whether blur-behind works here; checked once. Without it the app stays
// opaque.
bool available();

// Blurs behind `window` (a top-level) within `region`, in the window's own
// coordinates — the whole window if empty. A popup whose panel is smaller
// than its window (rounded corners, a shadow margin around it) passes the
// panel's shape, so the blur doesn't show past the panel's edges. Call
// again when the shape changes (e.g. on resize); a no-op when !available().
void enableBlurBehind(QWidget* window, const QRegion& region = QRegion());

// The region for blurring behind a rounded panel at `rect`, for
// enableBlurBehind() — a little inside it, so no blur shows past its edge.
QRegion roundedRegion(const QRect& rect, qreal radius);

} // namespace Integration::WindowGlass
