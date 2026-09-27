#pragma once

#include <QRegion>

class QWidget;

namespace Integration::WindowGlass {

// Asks the window system to blur what's behind a translucent top-level
// window — the "glass" the app's own tinted, partly transparent chrome is
// painted over (Theme::glassEnabled()). Platform-specific underneath:
//   - Wayland: the standard ext_background_effect_v1 protocol, where the
//     compositor offers it (KWin 6.5+ and others), else KWin's own
//     org_kde_kwin_blur.
//   - X11 under KDE: KWin's _KDE_NET_WM_BLUR_BEHIND_REGION window property.
//   Both spoken directly when built with Qt's private headers,
//   wayland-client and xcb (the AppImage is). Without them, on KDE:
//   KWindowEffects::enableBlurBehind() from the system's KF6WindowSystem,
//   loaded at runtime — which only loads next to the Qt it was built
//   against.
//   - GNOME/Mutter (so far) and other compositors without such a protocol:
//     no way for the app to ask for blur. The window can still be
//     translucent, for a desktop extension to blur (on GNOME, Blur my
//     Shell's per-application blur) — Support::SeeThrough.
//   - Windows / macOS: not yet — the place for DwmSetWindowAttribute(
//     DWMWA_SYSTEMBACKDROP_TYPE, acrylic) / an NSVisualEffectView behind the
//     window's content.

enum class Support {
    // The window system blurs on request (enableBlurBehind()).
    Blur,
    // A translucent window shows the desktop through it, but only something
    // outside the app can blur it — glass only if the user asks for it.
    SeeThrough,
    // No translucent windows at all: the app stays opaque.
    None,
};

// What glass can be here; checked once.
Support support();

// Blurs behind `window` (a top-level) within `region`, in the window's own
// coordinates — the whole window if empty. A popup whose panel is smaller
// than its window (rounded corners, a shadow margin around it) passes the
// panel's shape, so the blur doesn't show past the panel's edges. Call
// again when the shape changes (e.g. on resize); a no-op unless
// support() is Support::Blur.
void enableBlurBehind(QWidget* window, const QRegion& region = QRegion());

// The region for blurring behind a rounded panel at `rect`, for
// enableBlurBehind() — a little inside it, so no blur shows past its edge.
QRegion roundedRegion(const QRect& rect, qreal radius);

} // namespace Integration::WindowGlass
