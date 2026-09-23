#include "StyleSheet.h"

#include <QApplication>

#include "Icons.h"
#include "Metrics.h"
#include "Radius.h"
#include "Spacing.h"
#include "Tokens.h"

namespace Theme {

namespace {

// RULE FOR EVERY BLOCK BELOW: never write a bare Qt widget-class selector
// (`QSlider { ... }`, `QDialog { ... }`, `QToolBar { ... }`, ...).
// Theme::applyGlobalStyleSheet() installs this stylesheet at the
// QApplication level, which applies process-wide to literally every
// QWidget with no subtree opt-out — including widgets built by native/
// system dialogs this app invokes but doesn't own (QFileDialog,
// QColorDialog, QPrintDialog, ...), which are expected to stay fully
// native-looking. A bare type selector matches those too if they happen
// to use the same Qt widget class internally (confirmed in practice:
// QFileDialog's file list is a QTreeView, its bookmark sidebar a
// QListView, its zoom control a QSlider — all types this app also styles
// for its own UI). Always scope instead, by:
//   - `#objectName` for one specific widget instance (e.g. #sidebarView,
//     #trackListView) — set via `widget->setObjectName(...)` at construction.
//   - `[property="value"]` for a named category of instances that should
//     share one look (e.g. `variant` on buttons, `themed` on dialogs/
//     progress bars) — set via `widget->setProperty(...)`.
// The one deliberate exception is QMenu (`panelsBlock()` below): every
// QMenu in the app, including ones Qt constructs internally like
// QLineEdit's context menu, is SUPPOSED to be themed — see
// Theme::CloudMusStyle's class doc for why that one needs to be universal.
//
// The equivalent rule for FONTS (QApplication::setFont() in main.cpp,
// which has no scoping mechanism at all, not even this one) is
// Theme::useSystemFont() (Typography.h) — call it on any native dialog
// instance this app constructs itself.

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

QString buttonsBlock(const Palette& p)
{
    // QToolButton too (MainWindow's hamburger menu button) — same Icon
    // Button treatment as the QPushButton transport controls beside it.
    // Transparent at rest — background only appears on hover/press
    // (surface-300/400) — per the design system's Icon Button spec.
    // "play" (the accent-filled play/pause button) is the one
    // exception: it keeps a permanent accent fill, never transparent.
    //
    // Radius is each button's own exact half-dimension, not the
    // abstract Radius::full=999 token — matching the design system's
    // own QSS examples (e.g. "border-radius: 16px /* radius-full */"
    // for a 32px button), because Qt's style-sheet engine, unlike a
    // browser, does NOT clamp an oversized border-radius down to 50%:
    // a literal 999px radius silently fails to round the corners at
    // all under Fusion, rendering as its own small native corner
    // instead. "play" defaults to the 36px Icon Button radius (18) —
    // NowPlayingBar's accent-filled play/pause button is that size —
    // and #heroPlayButton below overrides it to 20 for HeroPanel's
    // bigger 40px one.
    //
    // "border: 0px solid transparent", not "border: none": Qt Style
    // Sheets only computes rounded corners against the *border* box, so
    // "none" (no border box at all) skips the radius math too.
    return QStringLiteral(R"(QPushButton[variant="icon"], QToolButton[variant="icon"] {
    background: transparent;
    border: 0px solid transparent;
    border-radius: %1px;
}
QPushButton[variant="icon"]:hover, QToolButton[variant="icon"]:hover { background: %2; }
QPushButton[variant="icon"]:pressed, QPushButton[variant="icon"]:checked,
QToolButton[variant="icon"]:pressed, QToolButton[variant="icon"]:checked { background: %3; }
QToolButton[variant="icon"]::menu-indicator { image: none; }
QPushButton[variant="play"] {
    background: %4;
    border: 0px solid transparent;
    border-radius: %1px;
}
QPushButton[variant="play"]:hover { background: %5; }
QPushButton[variant="play"]:pressed { background: %6; }
#heroPlayButton { border-radius: %7px; })")
        .arg(QString::number(Metrics::iconButtonSize / 2), hex(p.surface300), hex(p.surface400), hex(p.accent),
            hex(p.accentHover), hex(p.accentPressed))
        .arg(QString::number(Metrics::playButtonSize / 2));
}

QString textButtonsBlock(const Palette& p)
{
    // "border: 0px solid transparent", not "border: none": see the comment in
    // buttonsBlock() explaining why "none" skips the radius math.
    return QStringLiteral(R"(QPushButton[variant="primary"] {
    background: %1;
    color: %2;
    border: 0px solid transparent;
    border-radius: %3px;
    padding: %4px %5px;
}
QPushButton[variant="primary"]:hover { background: %6; }
QPushButton[variant="primary"]:pressed { background: %7; }
QPushButton[variant="secondary"] {
    background: %8;
    color: %9;
    border: 1px solid %10;
    border-radius: %3px;
    padding: %4px %5px;
}
QPushButton[variant="secondary"]:hover { background: %11; })")
        .arg(hex(p.accent), hex(p.onAccent), QString::number(Radius::sm), QString::number(Spacing::space2),
            QString::number(Spacing::space3), hex(p.accentHover), hex(p.accentPressed), hex(p.surface200), hex(p.ink))
        .arg(hex(p.border), hex(p.surface300));
}

QString toolBarBlock(const Palette& p)
{
    // Fusion paints QToolBar with its own native panel/gradient by default
    // (a visibly different shade from the surface-100 panels around it,
    // unlike Breeze, which happened to blend it in) — pin it to the same
    // panel-background token as everything else, flat, no separator/
    // handle bevel, so the bottom transport bar reads as part of the
    // window chrome instead of a distinct native toolbar. A hairline
    // border-top (design system's `border` token) is the one exception —
    // it's what visually separates the transport bar from the content
    // above it in the design mockup.
    return QStringLiteral(R"(QToolBar#transportToolBar {
    background: %1;
    border: none;
    border-top: 1px solid %2;
    spacing: 0px;
}
QToolBar#transportToolBar::separator { background: transparent; width: 0px; height: 0px; }
QToolBar#transportToolBar::handle { width: 0px; height: 0px; })")
        .arg(hex(p.surface100), hex(p.border));
}

QString panelsBlock(const Palette& p)
{
    // Background/border/radius are NOT set here:
    // Theme::CloudMusStyle now paints QMenu's whole
    // panel (PE_PanelMenu) with genuine rounding —
    // a plain QSS border-radius only draws a rounded
    // shape inside a still-rectangular opaque window
    // (confirmed in practice), it doesn't actually
    // mask the widget. Only per-item styling stays
    // in QSS.
    //
    // "border: 0px solid transparent", not "border: none" — see
    // buttonsBlock()'s comment on why not "none".
    return QStringLiteral(R"(#sourceAuthCard {
    background: %1;
    border: 1px solid %2;
    border-radius: %3px;
}
#toastLabel {
    background: %4;
    color: %5;
    border: 0px solid transparent;
    border-radius: %6px;
    padding: %7px %8px;
}
QMenu { color: %5; }
QMenu::item:selected { background: %9; }
#transportSeparator { color: %2; }
#secondaryLabel { color: %10; })")
        .arg(hex(p.surface200), hex(p.border), QString::number(Radius::md), hex(p.surface400), hex(p.ink),
            QString::number(Radius::sm), QString::number(Spacing::space2), QString::number(Spacing::space3),
            hex(p.surface300))
        .arg(hex(p.inkSecondary));
}

// Scoped to [themed="true"] (both busyIndicator_ instances opt in via
// setProperty), not a bare QProgressBar type selector — see this file's
// top-of-namespace rule comment.
QString progressBarBlock(const Palette& p)
{
    return QStringLiteral(R"(QProgressBar[themed="true"] {
    background: %1;
    border: none;
}
QProgressBar[themed="true"]::chunk { background: %2; })")
        .arg(hex(p.surface300), hex(p.accent));
}

QString sidebarTreeBlock(const Palette& p)
{
    const QString chevronClosed = iconAssetPath(QStringLiteral("chevron_right"), IconColor::InkSecondary, 12);
    const QString chevronOpen = iconAssetPath(QStringLiteral("expand_more"), IconColor::InkSecondary, 12);
    const QString chevronClosedSelected = iconAssetPath(QStringLiteral("chevron_right"), IconColor::Accent, 12);
    const QString chevronOpenSelected = iconAssetPath(QStringLiteral("expand_more"), IconColor::Accent, 12);

    // Scoped to #sidebarView, NOT a bare QTreeView type selector — a
    // QFileDialog's detail-view file listing is a QTreeView too, and a
    // bare-type rule here was painting its background/branch chevrons the
    // same as our own sidebar, inside a dialog meant to stay fully native.
    //
    // NavItemDelegate::paint() already fully draws the selected row's
    // background/text itself, but Fusion separately paints its own
    // native "current item" indicator using QPalette::Highlight/
    // HighlightedText, which QSS never touched — left at its default,
    // that renders as a stray blue sliver over our surface-400 fill.
    // selection-background-color/selection-color map straight onto
    // those two palette roles, so matching them to the same tone the
    // delegate already paints makes that native decoration blend in
    // instead of standing out as a different color.
    //
    // Explicit width/height, not just image: — without a fixed box,
    // Fusion sizes the branch indicator (and the gap it reserves
    // before the row's text) from the image's own pixel dimensions,
    // so iconAssetPath's 4x-oversampled source (needed for
    // crispness at a fractional display scale, see Icons.cpp) blew
    // the indicator up to 4x its intended 12px size instead of just
    // supersampling it. Pinning the box to 12px makes Qt scale the
    // oversampled image down into it instead.
    return QStringLiteral(R"(QTreeView#sidebarView {
    background: %1;
    border: none;
    outline: 0;
    selection-background-color: %6;
    selection-color: %7;
}
QTreeView#sidebarView::branch:closed:has-children {
    image: url("%2"); width: 12px; height: 12px;
}
QTreeView#sidebarView::branch:open:has-children {
    image: url("%3"); width: 12px; height: 12px;
}
QTreeView#sidebarView::branch:selected:closed:has-children {
    image: url("%4"); width: 12px; height: 12px;
}
QTreeView#sidebarView::branch:selected:open:has-children {
    image: url("%5"); width: 12px; height: 12px;
})")
        .arg(hex(p.surface100), chevronClosed, chevronOpen, chevronClosedSelected, chevronOpenSelected)
        .arg(hex(p.surface400), hex(p.accent));
}

// trackListView_ is otherwise fully unstyled (TrackRowDelegate self-paints
// every row, but the QListView's own background behind/around them was
// never touched) — left at Qt's default Fusion palette background, a
// light gray visibly mismatched against the rest of this dark-themed
// app. surface0 per the design mockup (pixel-sampled directly — the main
// content area and this list are both #14100D, while the sidebar/toolbar
// chrome is the lighter #1C1714 = surface100).
//
// Scoped to #trackListView, NOT a bare QListView type selector — same
// "a native dialog uses this widget type too" reasoning as
// sidebarTreeBlock()'s #sidebarView scoping (QFileDialog's bookmark
// sidebar is a QListView).
QString trackListBlock(const Palette& p)
{
    return QStringLiteral(R"(QListView#trackListView { background: %1; border: none; outline: 0; })")
        .arg(hex(p.surface0));
}

// Covers QLineEdit/QCheckBox app-wide by widget TYPE (so any future one —
// dialog or not — gets the design system's look for free), but the dialog
// window background itself is scoped to `QDialog[themed="true"]`, NOT
// a bare `QDialog` selector: QDialog is also the base class of QFileDialog
// (and QColorDialog/QFontDialog/etc.), so a bare-type rule was bleeding
// into the native folder picker — half-restyled (our dark background) but
// still built from dozens of native-icon-laden widgets we don't own and
// don't want to reskin. `themed` is the same "opt in via a dynamic
// property" convention buttons already use for `variant` — every CUSTOM
// app dialog (Ui::SettingsDialog, Ui::AboutDialog, and any future one)
// calls `setProperty("themed", true)` in its constructor; native/system
// dialogs never set it and stay fully native as intended.
//
// surface100 (the same "chrome" tone as the toolbar/sidebar) for the
// dialog's own window background: a utility window like Settings/About
// reads closer to the app's chrome than to main content (surface0).
// Fields/checkbox sit one tone up at surface200, the same "raised
// control" treatment #sourceAuthCard and the secondary button variant
// already use.
QString dialogsBlock(const Palette& p)
{
    const QString checkIcon = iconAssetPath(QStringLiteral("check"), IconColor::OnAccent, 12);

    return QStringLiteral(R"(QDialog[themed="true"] { background: %1; color: %2; }
QLineEdit {
    background: %3;
    border: 1px solid %4;
    border-radius: %5px;
    padding: %6px %7px;
    color: %2;
    selection-background-color: %8;
    selection-color: %9;
}
QLineEdit:focus { border: 1px solid %8; }
QLineEdit[variant="filter"] { padding: %7px %7px; border-radius: %12px; }
QCheckBox { spacing: %7px; color: %2; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border-radius: %5px;
    border: 1px solid %10;
    background: %3;
}
QCheckBox::indicator:hover { border-color: %8; }
QCheckBox::indicator:checked {
    background: %8;
    border-color: %8;
    image: url("%11");
})")
        .arg(hex(p.surface100), hex(p.ink), hex(p.surface200), hex(p.border), QString::number(Radius::sm),
            QString::number(Spacing::space1), QString::number(Spacing::space2), hex(p.accent), hex(p.onAccent))
        .arg(hex(p.borderStrong), checkIcon, QString::number(Radius::md));
}

} // namespace

QString buildStyleSheet(Mode mode)
{
    const Palette& p = palette(mode);
    // No scrollbar QSS block anymore: the app's only two scrollable views
    // (sidebarView_, trackListView_) both use Ui::OverlayScrollBar, which
    // hides the real QScrollBar entirely and paints its own floating
    // handle — a QSS rule for QScrollBar would never be reached.
    return buttonsBlock(p) + textButtonsBlock(p) + toolBarBlock(p) + panelsBlock(p) + progressBarBlock(p)
        + sidebarTreeBlock(p) + trackListBlock(p) + dialogsBlock(p);
}

void applyGlobalStyleSheet(QApplication& app)
{
    app.setStyleSheet(buildStyleSheet(currentMode()));
    QObject::connect(
        &notifier(), &Notifier::changed, &app, [&app]() { app.setStyleSheet(buildStyleSheet(currentMode())); });
}

} // namespace Theme
