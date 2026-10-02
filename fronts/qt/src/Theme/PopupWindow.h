#pragma once

#include <QRect>
#include <Qt>

class QWidget;

namespace Theme {

// How popup windows — menus, tooltips, the hover card, the downloads panel
// — get their shadow, rounded corners and glass, which depends on what the
// window system can do (Integration::WindowGlass):
enum class PopupLook {
    // The window is bigger than its panel: the shadow is hand-painted
    // around it (paintSoftShadow()), and with glass the blur is asked for
    // just the panel. Linux; Windows 10 without glass.
    Painted,
    // Windows 10 with glass: the blur covers whole windows only, so the
    // window is just the panel, clipped to its shape, and the shadow is
    // painted in a second window under it (blurring it would blur a
    // rectangle around the panel).
    Clipped,
    // Windows 11 22H2+: the window is just the panel; DWM rounds its
    // corners, shadows it and, with glass, puts acrylic behind it.
    Native,
};
PopupLook popupLook();

// The margin a popup reserves around its panel for the hand-painted
// shadow: `reach` when Painted, else none.
int popupShadowMargin(int reach);

// `flags` for a popup's window in this look: a Native popup must not be a
// layered window (Qt makes frameless translucent ones layered, and
// Qt::WindowTransparentForInput ones) for DWM to round, shadow and back
// it — a Qt::Popup/Qt::ToolTip window is borderless without
// Qt::FramelessWindowHint anyway. Elsewhere, no system shadow: ours.
Qt::WindowFlags popupWindowFlags(Qt::WindowFlags flags);

// Call in a popup's constructor, after its flags, before its native window
// exists. `clickThrough`: the mouse goes through it to what's under it, as
// with Qt::WindowTransparentForInput, which a Native popup can't have.
void preparePopup(QWidget* popup, bool clickThrough);

// How popups fade in and out.
enum class PopupFade {
    // Not at all: Painted popups over the compositor's blur, which can't
    // fade along with what we paint — the compositor fades those itself,
    // blur included, as it does menus.
    None,
    // By painting with opacity (setPopupOpacity() fades a Clipped popup's
    // shadow window along). Not by window opacity: Wayland has none.
    Painted,
    // By the window's opacity: Native popups, as QMenu's own fade does
    // there. DWM's backdrop and shadow can't fade along with what we
    // paint; this way the window is layered while it fades, without them,
    // and gets them back once opaque.
    Window,
};
PopupFade popupFade();

// A popup's hand-painted shadow (paintSoftShadow()).
struct PopupShadow {
    int reach;
    int offsetY;
    int maxAlpha;
};

// Call before the popup shows and whenever its panel changes (on resize):
// glass behind the panel at `panel` (the popup's coordinates) and, when
// Clipped, the shadow window under it. `radius`: the panel's corners.
void setUpPopup(QWidget* popup, const QRect& panel, int radius, const PopupShadow& shadow);

// The popup's opacity while it fades: its window's own when the fade is
// PopupFade::Window; a Clipped popup's shadow window fades along.
void setPopupOpacity(QWidget* popup, qreal opacity);

} // namespace Theme
