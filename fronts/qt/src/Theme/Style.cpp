#include "Style.h"

#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

#include "Radius.h"
#include "Tokens.h"
#include "Typography.h"

namespace Theme {

namespace {

// Reserved frame width (all four sides) the hand-painted shadow lives in —
// see Style.h's class doc for why a QGraphicsDropShadowEffect doesn't work
// here. Deliberately smaller than ToastNotifier/ThemedToolTipPopup's
// blur-radius-16 effect: that margin has to be real, opaque window space
// for a QMenu (there's no way to let it bleed past the window edge), so a
// tighter falloff is the practical tradeoff.
constexpr int kMenuShadowMargin = 12;
constexpr int kMenuShadowOffsetY = 2;
constexpr int kMenuShadowMaxAlpha = 32;

QPainterPath roundedPath(const QRectF& rect, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

QRect menuPanelRect(const QRect& widgetRect)
{
    return widgetRect.adjusted(kMenuShadowMargin, kMenuShadowMargin, -kMenuShadowMargin, -kMenuShadowMargin);
}

// Fake blur: concentric rounded-rect OUTLINES (not filled disks) growing
// outward from the real panel and fading out. Outlines, not fills, matter
// here — filled, overlapping rounded rects all share the same center, so
// every pixel near the panel edge sits inside ALL of them at once, and
// SourceOver-compositing that many semi-transparent fills on top of each
// other compounds their alpha (1-(1-a)^n) far past any single layer's own
// alpha — that compounding was the actual cause of the shadow reading as
// much too dark/harsh. A thin ring per spread step touches each pixel at
// most once, so the painted alpha is what it looks like.
void paintMenuShadow(QPainter* painter, const QRect& panelRect)
{
    painter->save();
    painter->setBrush(Qt::NoBrush);
    for (int spread = kMenuShadowMargin; spread >= 1; --spread) {
        const qreal t = qreal(spread) / kMenuShadowMargin; // 1 at the outer edge, ~0 near the panel
        const int alpha = qRound(kMenuShadowMaxAlpha * (1.0 - t) * (1.0 - t));
        if (alpha <= 0)
            continue;
        const QRect layerRect = panelRect.adjusted(-spread, -spread, spread, spread).translated(0, kMenuShadowOffsetY);
        // Width 2, one step apart: a 1px gap between consecutive rings
        // would show as faint seams: this overlaps them by ~1px instead.
        painter->setPen(QPen(QColor(0, 0, 0, alpha), 2));
        painter->drawPath(roundedPath(QRectF(layerRect), Radius::md + spread));
    }
    painter->restore();
}

} // namespace

void CloudMusStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    if (qobject_cast<QMenu*>(widget)) {
        widget->setAttribute(Qt::WA_TranslucentBackground);
        // "13px/600 — button labels, actionable menu items" is this design
        // system's own description of TextStyle::Button — Fusion's default
        // menu font otherwise stays whatever QApplication's own base font
        // is, never matching the design system's weight/size.
        widget->setFont(Theme::font(TextStyle::Button));
        // See eventFilter(): compensates for the position shift
        // PM_MenuPanelWidth's enlarged window causes.
        widget->installEventFilter(this);
    }
}

void CloudMusStyle::drawPrimitive(
    PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (element == PE_PanelMenu && qobject_cast<const QMenu*>(widget)) {
        painter->save();
        // QMenu repaints only the dirty sub-rect on every hover-highlight
        // change (not the whole widget), and the default SourceOver
        // composition mode BLENDS each repaint's antialiased edge pixels
        // onto whatever was already there — over a translucent widget,
        // that accumulates opacity along the rounded corners' antialiasing
        // with every repaint until they visibly "tear." Source (replace,
        // not blend) resets this widget's whole paint region to fully
        // transparent first, so every repaint starts from a clean slate
        // regardless of how many times it's been redrawn already.
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->fillRect(option->rect, Qt::transparent);
        painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect panelRect = menuPanelRect(option->rect);
        paintMenuShadow(painter, panelRect);

        const Palette& pal = palette();
        painter->setPen(QPen(pal.border, 1));
        painter->setBrush(pal.surface200);
        painter->drawPath(roundedPath(QRectF(panelRect).adjusted(0.5, 0.5, -0.5, -0.5), Radius::md));
        painter->restore();
        return;
    }

    if (element == PE_FrameMenu) {
        // No-op: PE_PanelMenu above already painted fill + border in one
        // pass — Fusion's own frame would otherwise draw a second,
        // rectangular border on top of it.
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void CloudMusStyle::drawControl(
    ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (element == CE_MenuItem && qobject_cast<const QMenu*>(widget)) {
        painter->save();
        const QRect panelRect = menuPanelRect(widget->rect());
        painter->setClipPath(
            roundedPath(QRectF(panelRect).adjusted(0.5, 0.5, -0.5, -0.5), Radius::md), Qt::IntersectClip);
        QProxyStyle::drawControl(element, option, painter, widget);
        painter->restore();
        return;
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

int CloudMusStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    // Reserves the margin paintMenuShadow() paints into — QMenu sizes
    // itself (and positions its items) from this metric the same way any
    // QFrame subtracts its own frameWidth, so increasing it grows the
    // window uniformly on all four sides without otherwise touching item
    // layout math (that's PM_MenuHMargin/VMargin's job, untouched here).
    if (metric == PM_MenuPanelWidth && qobject_cast<const QMenu*>(widget))
        return kMenuShadowMargin;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

bool CloudMusStyle::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show) {
        // QShowEvent is sent synchronously inside QWidget::setVisible(true),
        // before Qt actually maps the platform window — shifting geometry
        // here lands before anything is visible on screen, no flicker.
        //
        // Qt positioned this (already margin-enlarged, see pixelMetric())
        // window so ITS OWN top-left sits at the caller's intended point;
        // shift it back by the same margin so the visible rounded panel's
        // top-left — not the window's — is the one that ends up there.
        // Known limitation: if Qt instead anchored a different corner
        // (flipped near a screen edge to stay on-screen), this fixed
        // offset no longer matches which corner was anchored — accepted,
        // not the reported case and not fixable without private Qt state.
        if (auto* menu = qobject_cast<QMenu*>(watched))
            menu->move(menu->pos() - QPoint(kMenuShadowMargin, kMenuShadowMargin));
    }
    return QProxyStyle::eventFilter(watched, event);
}

} // namespace Theme
