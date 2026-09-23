#pragma once

#include <QProxyStyle>

namespace Theme {

// Wraps Fusion (see main.cpp's comment on why Fusion, not a native style)
// to reach the ONE class of widget QSS alone can't properly round:
// QMenu. QSS border-radius only draws a rounded shape inside a still-
// rectangular, opaque window (confirmed in practice) — genuine rounding
// needs WA_TranslucentBackground, which QSS has no way to request. A
// QStyle is also the only mechanism that reaches EVERY QMenu instance
// regardless of who constructs it, including ones built entirely inside
// Qt itself (e.g. QLineEdit's built-in right-click context menu) that the
// app never sees a pointer to and so could never subclass or otherwise
// intercept.
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
// Ui::ThemedSlider's handle length and themed QSplitters' 1px handle
// (see pixelMetric()/drawControl()), are overridden here. Everything
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
    // (now-bigger) window so ITS top-left lands at the caller's target
    // point, which leaves the visible rounded panel's own top-left
    // sitting one margin further down-right than intended. See Style.cpp.
    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace Theme
