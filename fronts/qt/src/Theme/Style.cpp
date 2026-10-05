#include "Style.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QSplitter>
#include <QStyleOption>
#include <QToolButton>
#include <QTreeView>
#include <QVariantAnimation>
#include <QWindow>

#include "FocusRing.h"
#include "Icons.h"
#include "Metrics.h"
#include "PopupWindow.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"
#include "WindowGlass.h"

namespace Theme {

namespace {

// Reserved frame width (all four sides) the hand-painted shadow lives in —
// see Style.h's class doc for why a QGraphicsDropShadowEffect doesn't work
// here. Deliberately smaller than ToastNotifier/ThemedToolTipPopup's
// blur-radius-16 effect: that margin has to be real, opaque window space
// for a QMenu (there's no way to let it bleed past the window edge), so a
// tighter falloff is the practical tradeoff.
constexpr int kMenuShadowReach = 12;
constexpr int kMenuShadowOffsetY = 2;
constexpr int kMenuShadowMaxAlpha = 32;

// The reserved width itself: none unless the shadow is painted in the
// menu's own window (Theme::PopupLook).
int menuShadowMargin() { return popupShadowMargin(kMenuShadowReach); }

PopupShadow menuShadow()
{
    // Stronger over glass: the panel no longer stands out by being
    // opaque, so the shadow has to carry its edge.
    return { kMenuShadowReach, kMenuShadowOffsetY, glassEnabled() ? kMenuShadowMaxAlpha * 7 / 4 : kMenuShadowMaxAlpha };
}

// Qt's own expand/collapse slide of a tree view lasts 250 ms (not
// configurable); the chevron turns for as long, so the two end together.
constexpr int kBranchTurnMs = 250;
constexpr int kBranchGlyphSide = 20;
constexpr char kBranchTurnProperty[] = "cloudmusBranchTurn";

// The sidebar tree (Ui::SidebarTreeView), by class name so Theme doesn't
// depend on Ui and a QFileDialog's tree stays plain Fusion.
bool isSidebarTree(const QWidget* widget) { return widget && widget->inherits("Ui::SidebarTreeView"); }

// Where popupMenu() keeps a menu's anchor, in global coordinates.
constexpr char kMenuAnchorProperty[] = "cloudmusMenuAnchor";

QPainterPath roundedPath(const QRectF& rect, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

QRect menuPanelRect(const QRect& widgetRect)
{
    return widgetRect.adjusted(menuShadowMargin(), menuShadowMargin(), -menuShadowMargin(), -menuShadowMargin());
}

void paintMenuShadow(QPainter* painter, const QRect& panelRect)
{
    const PopupShadow shadow = menuShadow();
    paintSoftShadow(painter, panelRect, menuShadowMargin(), shadow.offsetY, shadow.maxAlpha, Radius::md);
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
    int firstItemTop = menuShadowMargin();
    for (QAction* action : menu->actions()) {
        if (action->isVisible() && !action->isSeparator()) {
            firstItemTop = menu->actionGeometry(action).top();
            break;
        }
    }
    const QPoint rightOf = parentMenu->mapToGlobal(
        QPoint(parentPanel.right() + 1 + kGap - menuShadowMargin(), item.top() - firstItemTop));
    QPoint pos = rightOf;
    const QRect screen = menu->screen()->availableGeometry();
    const QRect panelAt = menuPanelRect(QRect(pos, menu->size()));
    if (panelAt.right() > screen.right()) {
        const int parentLeft = parentMenu->mapToGlobal(parentPanel.topLeft()).x();
        pos.setX(parentLeft - kGap - menu->width() + menuShadowMargin());
    }
    // Keep the panel (not the shadow) within the screen vertically.
    const int panelBottom = pos.y() + menu->height() - menuShadowMargin();
    if (panelBottom > screen.bottom() + 1)
        pos.ry() -= panelBottom - (screen.bottom() + 1);
    pos.setY(qMax(pos.y(), screen.top() - menuShadowMargin()));
    menu->move(pos);

    // Wayland: a client can't place its popups — the compositor does, from
    // an anchor rect in the parent's coordinates, and for a submenu Qt
    // hands it the parent's item rect, ignoring the move() above. Qt's
    // Wayland plugin takes these window properties over its own choice:
    // an anchor rect spanning the same two candidate spots — the submenu's
    // left edge at its right end, or (flipped when there's no room) its
    // right edge at its left end — at the height computed above.
    if (QWindow* window = menu->windowHandle()) {
        const int rightX = parentPanel.right() + 1 + kGap - menuShadowMargin();
        const int leftX = parentPanel.left() - kGap + menuShadowMargin();
        const QRect anchor(leftX, item.top() - firstItemTop, rightX - leftX, 1);
        window->setProperty("_q_waylandPopupAnchorRect", anchor);
        window->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(Qt::Edges(Qt::TopEdge | Qt::RightEdge)));
        window->setProperty("_q_waylandPopupGravity", QVariant::fromValue(Qt::Edges(Qt::BottomEdge | Qt::RightEdge)));
    }
}

// The anchor Qt placed `menu` against, in global coordinates: the point
// popupMenu() recorded, or the QToolButton the menu drops from. Else the
// cursor — menus Qt pops up itself (QLineEdit's, the tray icon's) open
// where it was clicked.
QRect menuAnchor(const QMenu* menu)
{
    const QVariant recorded = menu->property(kMenuAnchorProperty);
    if (recorded.isValid())
        return recorded.toRect();
    if (const auto* button = qobject_cast<const QToolButton*>(menu->parentWidget()); button && button->menu() == menu)
        return QRect(button->mapToGlobal(QPoint(0, 0)), button->size());
    return QRect(QCursor::pos(), QSize(1, 1));
}

// See CloudMusStyle::eventFilter().
QPoint anchorShift(const QMenu* menu)
{
    const QRect anchor = menuAnchor(menu);
    const QPoint pos = menu->pos();
    return QPoint(pos.x() >= anchor.left() ? -menuShadowMargin() : menuShadowMargin(),
        pos.y() >= anchor.top() ? -menuShadowMargin() : menuShadowMargin());
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

// The ring for a button-like or check-like widget, in its own bounds.
void paintWidgetFocusRing(QPainter* painter, const QStyleOption* option, const QWidget* widget)
{
    const QString variant = widget->property("variant").toString();
    QRectF shape = widget->rect();
    qreal radius = Radius::sm;
    bool onAccent = false;
    if (variant == QLatin1String("icon") || variant == QLatin1String("play")) {
        radius = qMin(shape.width(), shape.height()) / 2.0;
        if (widget->property("compact").toBool())
            radius = 10;
        onAccent = variant == QLatin1String("play");
    } else if (variant == QLatin1String("filter")) {
        radius = Radius::md;
    } else if (variant == QLatin1String("primary")) {
        onAccent = true;
    } else if (!(qobject_cast<const QAbstractButton*>(widget) && variant.isEmpty())) {
        // Check boxes and radio buttons: the whole row. Anything else: the
        // rect the style was asked about.
        if (option->rect.isValid())
            shape = QRectF(option->rect);
    }
    if (onAccent) {
        // Inside the fill, where the contrast is.
        shape.adjust(2, 2, -2, -2);
        radius = qMax<qreal>(0, radius - 2);
    }
    paintFocusRing(painter, shape, radius, onAccent);
}

} // namespace

void CloudMusStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    // Rows slide open and closed. QTreeView takes this from the style's
    // SH_Widget_Animation_Duration once, in its constructor — too early
    // for a subclass's name to match, hence here. QCommonStyle says no for
    // every tree; opting in is the point.
    if (auto* tree = qobject_cast<QTreeView*>(widget); tree && isSidebarTree(tree)) {
        tree->setAnimated(true);
        // The chevron turns from the view's own signals, not on noticing a
        // changed state while painting: that would come a frame or more
        // after the rows start sliding. Once only, polish() repeats.
        if (!tree->property(kBranchTurnProperty).toBool()) {
            tree->setProperty(kBranchTurnProperty, true);
            connect(tree, &QTreeView::expanded, this,
                [this, tree](const QModelIndex& index) { turnBranch(tree, index, true); });
            connect(tree, &QTreeView::collapsed, this,
                [this, tree](const QModelIndex& index) { turnBranch(tree, index, false); });
            connect(tree, &QObject::destroyed, this, [this, tree] {
                // Their finished() would reach into the dead view.
                qDeleteAll(turns_.take(tree));
            });
        }
    }

    if (qobject_cast<QMenu*>(widget)) {
        // Windows gives a top-level window per-pixel alpha only when it's
        // also frameless — Qt::Popup alone isn't, and the shadow margin
        // came out black there. No native drop shadow either: Windows
        // would put a rectangular one around the whole window, and
        // drawPrimitive() paints our own. Except Windows 11's own popup
        // look, which needs the opposite — popupWindowFlags() knows.
        // Checked first: setWindowFlags() re-parents, not something to
        // repeat on every re-polish.
        const Qt::WindowFlags flags = popupWindowFlags(widget->windowFlags() | Qt::FramelessWindowHint);
        if (widget->windowFlags() != flags)
            widget->setWindowFlags(flags);
        preparePopup(widget, /*clickThrough=*/false);
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
    if (qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)) {
        // The wheel only reaches one that has the focus (see eventFilter()),
        // so it must not take it from the wheel itself, as WheelFocus does.
        widget->setFocusPolicy(Qt::StrongFocus);
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
        painter->setBrush(glass(pal.surface200));
        painter->drawPath(roundedPath(QRectF(panelRect).adjusted(0.5, 0.5, -0.5, -0.5), Radius::md));
        painter->restore();
        return;
    }

    if (element == PE_IndicatorBranch && isSidebarTree(widget)) {
        paintSidebarBranch(option, painter, qobject_cast<const QAbstractItemView*>(widget));
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

    // Fusion's dotted rectangle (a square box around even the round
    // buttons) gives way to a ring that follows the control's own shape,
    // shown only while navigating by keyboard.
    if (element == PE_FrameFocusRect && widget != nullptr && widget->window() != nullptr) {
        const auto* dialog = qobject_cast<const QDialog*>(widget->window());
        if (dialog == nullptr || dialog->property("themed").toBool()) {
            if (focusVisible(widget))
                paintWidgetFocusRing(painter, option, widget);
            return;
        }
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void CloudMusStyle::paintSidebarBranch(
    const QStyleOption* option, QPainter* painter, const QAbstractItemView* view) const
{
    // Nothing for the indentation columns of deeper rows, nor for rows
    // without children (State_Children).
    if (view == nullptr || !(option->state & State_Children))
        return;
    // Turning, if the row was just opened or closed (see turnBranch()),
    // otherwise at rest. The option has no index: ask the view what is at
    // the rect — its coordinates are the viewport's even while Qt paints
    // the slide's snapshots of the tree.
    qreal angle = (option->state & State_Open) ? 90.0 : 0.0;
    const auto rows = turns_.constFind(view);
    if (rows != turns_.constEnd()) {
        const auto turn = rows->constFind(QPersistentModelIndex(view->indexAt(option->rect.center())));
        if (turn != rows->constEnd())
            angle = (*turn)->currentValue().toReal();
    }

    const QIcon glyph = icon(QStringLiteral("chevron_right"),
        (option->state & State_Selected) ? IconColor::Accent : IconColor::InkSecondary, kBranchGlyphSide);
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->translate(QRectF(option->rect).center());
    // Mirrored in a right-to-left layout: closed points left.
    if (option->direction == Qt::RightToLeft)
        painter->scale(-1, 1);
    painter->rotate(angle);
    glyph.paint(painter, QRect(-kBranchGlyphSide / 2, -kBranchGlyphSide / 2, kBranchGlyphSide, kBranchGlyphSide));
    painter->restore();
}

void CloudMusStyle::turnBranch(QTreeView* view, const QModelIndex& index, bool open)
{
    // Rows opened or closed in code (startup, a refresh) appear in place,
    // like their slide: see MainWindow::restoreExpansion().
    if (!view->isAnimated())
        return;
    auto& rows = turns_[view];
    const QPersistentModelIndex key(index);
    QVariantAnimation*& turn = rows[key];
    // From wherever it is now, so a quick second click turns it back
    // smoothly instead of jumping first.
    const qreal from = turn != nullptr ? turn->currentValue().toReal() : (open ? 0.0 : 90.0);
    if (turn == nullptr) {
        turn = new QVariantAnimation(this);
        turn->setEasingCurve(QEasingCurve::InOutQuad);
        turn->setDuration(kBranchTurnMs);
        // The row, not the whole tree: a repaint per frame.
        connect(turn, &QVariantAnimation::valueChanged, turn, [view = QPointer(view), key] {
            if (view && key.isValid()) {
                const QRect row = view->visualRect(QModelIndex(key));
                view->viewport()->update(0, row.y(), view->viewport()->width(), row.height());
            }
        });
        // Back to State_Open's own angle once it is there.
        connect(turn, &QVariantAnimation::finished, this, [this, view = QPointer(view), key, turn] {
            if (view)
                turns_[view.data()].remove(key);
            turn->deleteLater();
            if (view)
                view->viewport()->update();
        });
    }
    turn->stop();
    turn->setStartValue(from);
    turn->setEndValue(open ? 90.0 : 0.0);
    turn->start();
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
            painter->setBrush(selectedFill(palette().surface200));
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
        return menuShadowMargin();
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
    // Scrolling a page with the wheel must not change a combo box or a spin
    // box the pointer happens to pass over: only one that has the focus
    // takes the wheel, the rest hand it to their parent to scroll.
    if (event->type() == QEvent::Wheel) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && !widget->hasFocus() && widget->parentWidget()
            && (qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget))) {
            event->ignore();
            QCoreApplication::sendEvent(widget->parentWidget(), event);
            return true;
        }
    }
    // The menu's glass and shadow (setUpPopup()), again as its size
    // changes. On Show, before the menu is on screen: Windows 11's own
    // popup look must be set by then; blur waits for the surface by itself.
    if (event->type() == QEvent::Show || event->type() == QEvent::Resize) {
        if (auto* menu = qobject_cast<QMenu*>(watched))
            setUpPopup(menu, menuPanelRect(menu->rect()), Radius::md, menuShadow());
    }
    if (event->type() == QEvent::Show) {
        // QShowEvent is sent synchronously inside QWidget::setVisible(true),
        // before Qt actually maps the platform window — shifting geometry
        // here lands before anything is visible on screen, no flicker.
        //
        // Qt positioned this (already margin-enlarged, see pixelMetric())
        // window against the caller's anchor: below and right of it
        // normally, but ABOVE it (window bottom at the anchor) when there's
        // no room below, and left of it when there's none on the right.
        // Shift it by the margin towards the anchor on each axis, so the
        // visible rounded panel's edge, not the window's, ends up there — a
        // fixed up-left shift put an upward-flipped menu two margins above
        // its button.
        //
        // A submenu is placed from scratch instead (see placeSubmenu()):
        // Qt put it by the parent's item, not at a point of the caller's.
        //
        // The size is put back after the move: a menu made at startup (the
        // menu button's) can still think it's on the screen it was made on,
        // and when the move takes it across to its real one, Qt 6.9 (the
        // AppImage's) resets the window to QWidget's default 100x30 — an
        // empty strip with no items.
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            const QSize size = menu->size();
            const auto* parentMenu = qobject_cast<const QMenu*>(menu->parentWidget());
            if (parentMenu && parentMenu->isVisible() && parentMenu->actions().contains(menu->menuAction()))
                placeSubmenu(menu, parentMenu);
            else
                menu->move(menu->pos() + anchorShift(menu));
            if (menu->size() != size)
                menu->resize(size);
        }
    }
    return QProxyStyle::eventFilter(watched, event);
}

void popupMenu(QMenu* menu, const QPoint& globalPos)
{
    menu->setProperty(kMenuAnchorProperty, QRect(globalPos, QSize(1, 1)));
    menu->popup(globalPos);
}

} // namespace Theme
