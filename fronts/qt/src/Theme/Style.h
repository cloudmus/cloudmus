#pragma once

#include <QProxyStyle>

class QMenu;
class QPoint;
class QStyleOptionMenuItem;

namespace Theme {

// Wraps Fusion (see main.cpp's comment on why Fusion, not a native style)
// to reach the ONE class of widget QSS alone can't properly round:
// QMenu. QSS border-radius only draws a rounded shape inside a still-
// rectangular, opaque window (confirmed in practice) — genuine rounding
// needs WA_TranslucentBackground (and, for Windows, a frameless window),
// which QSS has no way to request. A QStyle is also the only mechanism
// that reaches EVERY QMenu instance regardless of who constructs it,
// including ones built entirely inside Qt itself (e.g. QLineEdit's
// built-in right-click context menu) that the app never sees a pointer
// to and so could never subclass or otherwise intercept.
//
// The drop shadow is hand-painted in drawPrimitive() rather than a
// QGraphicsDropShadowEffect: an effect's blur bleeds outside the widget's
// own geometry, but a top-level window can't paint beyond its own frame,
// so the blur just gets clipped away at the window edge — invisible in
// practice. pixelMetric(PM_MenuPanelWidth) instead reserves real margin
// around the menu's content for the shadow to live in, same idea as any
// QFrame's own frame width.
//
// Scope is intentionally narrow: only QMenu's panel/frame drawing and
// polish, plus the slider click behavior hint (see styleHint()),
// Ui::ThemedSlider's handle length, themed QSplitters' 1px handle
// (see pixelMetric()/drawControl()) and check box / radio indicators
// inside menus (drawPrimitive()), are overridden here. Everything
// else falls through to Fusion unchanged.
class CloudMusStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void polish(QWidget* widget) override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
        const QWidget* widget = nullptr) const override;

    // Clips CE_MenuItem's own (rectangular) highlight painting to the
    // panel's rounded shape — otherwise Fusion's selected-item fill runs
    // edge-to-edge and visibly overhangs past the rounded corners for the
    // first/last row.
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
        const QWidget* widget = nullptr) const override;

    // CE_MenuItem via Fusion, but with this style's radio indicator for
    // exclusive (radio) items — see the .cpp.
    void drawMenuItemWithIndicator(const QStyleOptionMenuItem* item, QPainter* painter, const QWidget* widget) const;

    int pixelMetric(
        PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;

    // Qt's default QSlider only jumps straight to the clicked point on a
    // middle-click; a left-click on the groove instead takes a single page
    // step towards it, which reads as "the handle didn't move" and makes
    // the seek/volume sliders feel like they must be dragged by the handle.
    // Adding the left button to SH_Slider_AbsoluteSetButtons reuses Qt's own
    // click-to-position path, so dragging onward from that click still works.
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
        QStyleHintReturn* returnData = nullptr) const override;

    // Compensates for pixelMetric()'s PM_MenuPanelWidth growing the menu's
    // window by the shadow margin on every side: Qt positions that
    // (now-bigger) window with one of ITS edges at the caller's anchor,
    // which leaves the visible rounded panel one margin away from it.
    // See Style.cpp.
    bool eventFilter(QObject* watched, QEvent* event) override;
};

// QMenu::popup(), remembering `globalPos` as the anchor — so
// CloudMusStyle can tell which side of it Qt put the menu on (flipped up
// or left when there's no room) and place the panel, not its shadow
// margin, right at it. Use for every menu popped up at a point.
void popupMenu(QMenu* menu, const QPoint& globalPos);

} // namespace Theme
