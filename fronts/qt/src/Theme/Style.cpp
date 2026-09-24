#include "Style.h"

#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSplitter>
#include <QStyleOption>
#include <QWindow>

#include "Metrics.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
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

// Places a submenu (window already sized, not yet mapped) beside the
// parent's panel: its visible panel a small gap off the parent panel's
// right edge — or its left one when there's no room on the right — and
// its first item level with the item that opened it. Qt's own placement
// counts from window edges, i.e. from the shadow margins on both menus.
void placeSubmenu(QMenu* menu, const QMenu* parentMenu)
{
    constexpr int kGap = Spacing::space1;
    const QRect item = parentMenu->actionGeometry(menu->menuAction());
    const QRect parentPanel = menuPanelRect(parentMenu->rect());
    int firstItemTop = kMenuShadowMargin;
    for (QAction* action : menu->actions()) {
        if (action->isVisible() && !action->isSeparator()) {
            firstItemTop = menu->actionGeometry(action).top();
            break;
        }
    }
    const QPoint rightOf = parentMenu->mapToGlobal(
        QPoint(parentPanel.right() + 1 + kGap - kMenuShadowMargin, item.top() - firstItemTop));
    QPoint pos = rightOf;
    const QRect screen = menu->screen()->availableGeometry();
    const QRect panelAt = menuPanelRect(QRect(pos, menu->size()));
    if (panelAt.right() > screen.right()) {
        const int parentLeft = parentMenu->mapToGlobal(parentPanel.topLeft()).x();
        pos.setX(parentLeft - kGap - menu->width() + kMenuShadowMargin);
    }
    // Keep the panel (not the shadow) within the screen vertically.
    const int panelBottom = pos.y() + menu->height() - kMenuShadowMargin;
    if (panelBottom > screen.bottom() + 1)
        pos.ry() -= panelBottom - (screen.bottom() + 1);
    pos.setY(qMax(pos.y(), screen.top() - kMenuShadowMargin));
    menu->move(pos);

    // Wayland: a client can't place its popups — the compositor does, from
    // an anchor rect in the parent's coordinates, and for a submenu Qt
    // hands it the parent's item rect, ignoring the move() above. Qt's
    // Wayland plugin takes these window properties over its own choice:
    // an anchor rect spanning the same two candidate spots — the submenu's
    // left edge at its right end, or (flipped when there's no room) its
    // right edge at its left end — at the height computed above.
    if (QWindow* window = menu->windowHandle()) {
        const int rightX = parentPanel.right() + 1 + kGap - kMenuShadowMargin;
        const int leftX = parentPanel.left() - kGap + kMenuShadowMargin;
        const QRect anchor(leftX, item.top() - firstItemTop, rightX - leftX, 1);
        window->setProperty("_q_waylandPopupAnchorRect", anchor);
        window->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(Qt::Edges(Qt::TopEdge | Qt::RightEdge)));
        window->setProperty("_q_waylandPopupGravity", QVariant::fromValue(Qt::Edges(Qt::BottomEdge | Qt::RightEdge)));
    }
}

// A menu itself, or a widget inside one (e.g. a QCheckBox row put into a
// QMenu via QWidgetAction).
bool inMenu(const QWidget* widget)
{
    for (const QWidget* w = widget; w != nullptr; w = w->parentWidget()) {
        if (qobject_cast<const QMenu*>(w))
            return true;
    }
    return false;
}

constexpr int kIndicatorSide = 16;

// The design system's check box / radio button: a rounded square or a
// circle, outlined at rest (stronger on hover), filled with the accent
// and marked in on-accent when checked; dimmed when disabled.
void paintIndicator(QPainter* painter, const QStyleOption* option, bool radio)
{
    const Palette& pal = palette();
    const QRect bounds = option->rect;
    const int side = qMin(kIndicatorSide, qMin(bounds.width(), bounds.height()));
    const QRectF box(
        bounds.x() + (bounds.width() - side) / 2.0, bounds.y() + (bounds.height() - side) / 2.0, side, side);
    const bool on = option->state & QStyle::State_On;
    const bool partial = option->state & QStyle::State_NoChange;
    const bool hovered = option->state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (!(option->state & QStyle::State_Enabled))
        painter->setOpacity(0.45);

    const qreal radius = radio ? side / 2.0 : Radius::sm;
    const QRectF shape = box.adjusted(0.75, 0.75, -0.75, -0.75);
    if (on || partial) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(hovered ? pal.accentHover : pal.accent);
        painter->drawRoundedRect(shape, radius, radius);
        painter->setBrush(Qt::NoBrush);
        QPen mark(pal.onAccent, side / 8.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (radio) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(pal.onAccent);
            painter->drawEllipse(box.center(), side * 0.2, side * 0.2);
        } else if (partial) {
            painter->setPen(mark);
            painter->drawLine(QPointF(box.left() + side * 0.28, box.center().y()),
                QPointF(box.right() - side * 0.28, box.center().y()));
        } else {
            painter->setPen(mark);
            QPainterPath check;
            check.moveTo(box.left() + side * 0.26, box.top() + side * 0.52);
            check.lineTo(box.left() + side * 0.43, box.top() + side * 0.69);
            check.lineTo(box.left() + side * 0.75, box.top() + side * 0.33);
            painter->drawPath(check);
        }
    } else {
        painter->setPen(QPen(hovered ? pal.inkSecondary : pal.borderStrong, 1.5));
        painter->setBrush(pal.surface200);
        painter->drawRoundedRect(shape, radius, radius);
    }
    painter->restore();
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

    // Check boxes / radio buttons in menus: QCheckBox/QRadioButton rows,
    // and checkable menu items (Fusion draws those through the same
    // primitives).
    if ((element == PE_IndicatorCheckBox || element == PE_IndicatorRadioButton || element == PE_IndicatorMenuCheckMark)
        && inMenu(widget)) {
        paintIndicator(painter, option, element == PE_IndicatorRadioButton);
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
        // The hovered item gets only a tinted, rounded fill: Fusion's own
        // selected look adds an outlined frame around it. Paint the fill
        // here, then let Fusion draw the item as unselected (icon, text,
        // check, shortcut) on top of it.
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (item != nullptr && (item->state & State_Selected) && (item->state & State_Enabled)
            && item->menuItemType != QStyleOptionMenuItem::Separator) {
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette().surface400);
            // Full item width: PM_MenuHMargin/VMargin (see pixelMetric())
            // already inset every item equally from the panel's edges.
            painter->drawRoundedRect(QRectF(item->rect), Radius::sm, Radius::sm);
            QStyleOptionMenuItem unselected = *item;
            unselected.state &= ~State_Selected;
            drawMenuItemWithIndicator(&unselected, painter, widget);
        } else if (item != nullptr) {
            drawMenuItemWithIndicator(item, painter, widget);
        } else {
            QProxyStyle::drawControl(element, option, painter, widget);
        }
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

void CloudMusStyle::drawMenuItemWithIndicator(
    const QStyleOptionMenuItem* item, QPainter* painter, const QWidget* widget) const
{
    // Fusion draws an exclusive (radio) item's mark itself — a plain dot,
    // not through PE_IndicatorRadioButton — so have it draw the item
    // unmarked and paint the radio indicator into the same check rect it
    // uses (qfusionstyle.cpp's CE_MenuItem).
    if (item->checkType != QStyleOptionMenuItem::Exclusive) {
        QProxyStyle::drawControl(CE_MenuItem, item, painter, widget);
        return;
    }
    QStyleOptionMenuItem unmarked = *item;
    unmarked.checked = false;
    QProxyStyle::drawControl(CE_MenuItem, &unmarked, painter, widget);
    QStyleOption indicator = *item;
    indicator.rect
        = visualRect(item->direction, item->rect, QRect(item->rect.left() + 7, item->rect.center().y() - 6, 14, 14));
    indicator.state &= ~(State_On | State_Off | State_MouseOver);
    indicator.state |= item->checked ? State_On : State_Off;
    paintIndicator(painter, &indicator, /*radio=*/true);
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
    // Equal breathing room on every side between the panel and its items,
    // so the first/last item's hover fill doesn't run into the panel's
    // rounded edge.
    if ((metric == PM_MenuHMargin || metric == PM_MenuVMargin) && qobject_cast<const QMenu*>(widget))
        return Spacing::space1;
    // Matched by class name so Theme doesn't depend on Ui, and so other
    // QSliders (e.g. QFileDialog's zoom slider) keep Fusion's own metric.
    // <= 1 switches QSplitterHandle into its built-in "tiny mode": the
    // handle takes 1px in the layout, but its widget grows 2px contents
    // margins on each side over the neighboring panes, masked so only the
    // 1px line paints while the whole 5px still grabs the mouse.
    if (metric == PM_SplitterWidth && isThemedSplitter(widget))
        return 1;
    if ((metric == PM_IndicatorWidth || metric == PM_IndicatorHeight || metric == PM_ExclusiveIndicatorWidth
            || metric == PM_ExclusiveIndicatorHeight)
        && inMenu(widget))
        return kIndicatorSide;
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
        //
        // A submenu is placed from scratch instead (see placeSubmenu()):
        // Qt put it by the parent's item, not at a point of the caller's.
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            const auto* parentMenu = qobject_cast<const QMenu*>(menu->parentWidget());
            if (parentMenu && parentMenu->isVisible() && parentMenu->actions().contains(menu->menuAction()))
                placeSubmenu(menu, parentMenu);
            else
                menu->move(menu->pos() - QPoint(kMenuShadowMargin, kMenuShadowMargin));
        }
    }
    return QProxyStyle::eventFilter(watched, event);
}

} // namespace Theme
