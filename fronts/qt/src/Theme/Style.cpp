#include "Style.h"

#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QStyleOption>

#include "Metrics.h"
#include "Radius.h"
#include "Shadow.h"
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

void paintMenuShadow(QPainter* painter, const QRect& panelRect)
{
    paintSoftShadow(painter, panelRect, kMenuShadowMargin, kMenuShadowOffsetY, kMenuShadowMaxAlpha, Radius::md);
}

// App-owned splitters opt in via setProperty("themed", true) — same
// convention as themed dialogs/progress bars (see StyleSheet.cpp), so a
// native dialog's own QSplitter (e.g. QFileDialog's) keeps Fusion's look.
// Qt hands the style either the QSplitter itself or one of its handles.
bool isThemedSplitter(const QWidget* widget)
{
    const QSplitter* splitter = qobject_cast<const QSplitter*>(widget);
    if (!splitter) {
        if (const auto* handle = qobject_cast<const QSplitterHandle*>(widget))
            splitter = handle->splitter();
    }
    return splitter && splitter->property("themed").toBool();
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

    if (element == CE_Splitter && isThemedSplitter(widget)) {
        // option->rect is the handle's contentsRect() — just the 1px line,
        // not the wider grab area around it (see pixelMetric()).
        painter->fillRect(option->rect, palette().border);
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
    // Matched by class name so Theme doesn't depend on Ui, and so other
    // QSliders (e.g. QFileDialog's zoom slider) keep Fusion's own metric.
    // <= 1 switches QSplitterHandle into its built-in "tiny mode": the
    // handle takes 1px in the layout, but its widget grows 2px contents
    // margins on each side over the neighboring panes, masked so only the
    // 1px line paints while the whole 5px still grabs the mouse.
    if (metric == PM_SplitterWidth && isThemedSplitter(widget))
        return 1;
    if (metric == PM_SliderLength && widget && widget->inherits("Ui::ThemedSlider"))
        return Metrics::sliderHandleDiameter;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

int CloudMusStyle::styleHint(
    StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
{
    if (hint == SH_Slider_AbsoluteSetButtons)
        return Qt::LeftButton | Qt::MiddleButton;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
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
