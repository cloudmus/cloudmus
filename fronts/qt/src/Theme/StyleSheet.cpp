#include "StyleSheet.h"

#include <QApplication>

#include "Icons.h"
#include "Metrics.h"
#include "Radius.h"
#include "Spacing.h"
#include "Tokens.h"

namespace Theme {

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

QString buttonsBlock(const Palette& p)
{
    return QStringLiteral(
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
        "QPushButton[variant=\"icon\"], QToolButton[variant=\"icon\"] {"
        "    background: transparent;"
        "    border: 0px solid transparent;"
        "    border-radius: %1px;"
        "}"
        "QPushButton[variant=\"icon\"]:hover, QToolButton[variant=\"icon\"]:hover { background: %2; }"
        "QPushButton[variant=\"icon\"]:pressed, QPushButton[variant=\"icon\"]:checked,"
        "QToolButton[variant=\"icon\"]:pressed, QToolButton[variant=\"icon\"]:checked { background: %3; }"
        "QToolButton[variant=\"icon\"]::menu-indicator { image: none; }"
        "QPushButton[variant=\"play\"] {"
        "    background: %4;"
        "    border: 0px solid transparent;"
        "    border-radius: %1px;"
        "}"
        "QPushButton[variant=\"play\"]:hover { background: %5; }"
        "QPushButton[variant=\"play\"]:pressed { background: %6; }"
        "#heroPlayButton { border-radius: %7px; }")
        .arg(QString::number(Metrics::iconButtonSize / 2), hex(p.surface300), hex(p.surface400), hex(p.accent),
            hex(p.accentHover), hex(p.accentPressed))
        .arg(QString::number(Metrics::playButtonSize / 2));
}

QString textButtonsBlock(const Palette& p)
{
    return QStringLiteral("QPushButton[variant=\"primary\"] {"
                          "    background: %1;"
                          "    color: %2;"
                          "    border: 0px solid transparent;" // see buttonsBlock()'s comment on why not "none"
                          "    border-radius: %3px;"
                          "    padding: %4px %5px;"
                          "}"
                          "QPushButton[variant=\"primary\"]:hover { background: %6; }"
                          "QPushButton[variant=\"primary\"]:pressed { background: %7; }"
                          "QPushButton[variant=\"secondary\"] {"
                          "    background: %8;"
                          "    color: %9;"
                          "    border: 1px solid %10;"
                          "    border-radius: %3px;"
                          "    padding: %4px %5px;"
                          "}"
                          "QPushButton[variant=\"secondary\"]:hover { background: %11; }")
        .arg(hex(p.accent), hex(p.onAccent), QString::number(Radius::sm), QString::number(Spacing::space2),
            QString::number(Spacing::space3), hex(p.accentHover), hex(p.accentPressed), hex(p.surface200), hex(p.ink))
        .arg(hex(p.border), hex(p.surface300));
}

QString slidersBlock(const Palette& p)
{
    return QStringLiteral(
        "QSlider::groove:horizontal {"
        "    height: 3px;"
        "    background: %1;"
        "    border-radius: 1px;"
        "}"
        "QSlider::groove:horizontal:hover { background: %2; }"
        "QSlider::sub-page:horizontal { background: %3; border-radius: 1px; }"
        "QSlider::add-page:horizontal { background: transparent; }"
        "QSlider::handle:horizontal {"
        "    width: 12px; height: 12px; margin: -5px 0;"
        "    border-radius: 6px;"
        "    background: %3;"
        "}"
        "QSlider::handle:horizontal:hover { background: %4; }"
        "QSlider[handleVisible=\"false\"]::handle:horizontal { background: transparent; }"
        "QSlider#volumeSlider::sub-page:horizontal { background: %5; }"
        "QSlider#volumeSlider::handle:horizontal { background: %6; }"
        "QSlider#volumeSlider::handle:horizontal:hover { background: %7; }"
        // An ID selector (#volumeSlider) outranks a bare attribute
        // selector (QSlider[handleVisible="false"]) in Qt's CSS-like
        // specificity, so without repeating the ID here, the rule two
        // lines up unconditionally wins and the volume handle never
        // actually hides — this is the one that has to.
        "QSlider#volumeSlider[handleVisible=\"false\"]::handle:horizontal { background: transparent; }")
        .arg(hex(p.border), hex(p.borderStrong), hex(p.accent), hex(p.accentHover), hex(p.inkSecondary), hex(p.ink),
            hex(p.ink));
}

QString scrollbarBlock(const Palette& p)
{
    return QStringLiteral("QScrollBar:vertical {"
                          "    width: 6px;"
                          "    background: transparent;"
                          "    margin: 0;"
                          "}"
                          "QScrollBar::handle:vertical {"
                          "    background: %1;"
                          "    border-radius: 3px;"
                          "    min-height: 24px;"
                          "}"
                          "QScrollBar::handle:vertical:hover { background: %2; }"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                          "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(hex(p.borderStrong), hex(p.inkSecondary));
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
    return QStringLiteral("QToolBar {"
                          "    background: %1;"
                          "    border: none;"
                          "    border-top: 1px solid %2;"
                          "    spacing: 0px;"
                          "}"
                          "QToolBar::separator { background: transparent; width: 0px; height: 0px; }"
                          "QToolBar::handle { width: 0px; height: 0px; }")
        .arg(hex(p.surface100), hex(p.border));
}

QString panelsBlock(const Palette& p)
{
    return QStringLiteral("#sourceAuthCard {"
                          "    background: %1;"
                          "    border: 1px solid %2;"
                          "    border-radius: %3px;"
                          "}"
                          "#toastLabel {"
                          "    background: %4;"
                          "    color: %5;"
                          "    border: 0px solid transparent;" // see buttonsBlock()'s comment on why not "none"
                          "    border-radius: %6px;"
                          "    padding: %7px %8px;"
                          "}"
                          "QMenu {"
                          "    background: %1;"
                          "    border: 1px solid %2;"
                          "    color: %5;"
                          "}"
                          "QMenu::item:selected { background: %9; }"
                          "#transportSeparator { color: %2; }"
                          "#secondaryLabel { color: %10; }")
        .arg(hex(p.surface200), hex(p.border), QString::number(Radius::md), hex(p.surface400), hex(p.ink),
            QString::number(Radius::sm), QString::number(Spacing::space2), QString::number(Spacing::space3),
            hex(p.surface300))
        .arg(hex(p.inkSecondary));
}

QString progressBarBlock(const Palette& p)
{
    return QStringLiteral("QProgressBar {"
                          "    background: %1;"
                          "    border: none;"
                          "}"
                          "QProgressBar::chunk { background: %2; }")
        .arg(hex(p.surface300), hex(p.accent));
}

QString sidebarTreeBlock(const Palette& p)
{
    const QString chevronClosed = chevronAssetPath(QStringLiteral("chevron_right"), IconColor::InkSecondary, 12);
    const QString chevronOpen = chevronAssetPath(QStringLiteral("expand_more"), IconColor::InkSecondary, 12);
    const QString chevronClosedSelected = chevronAssetPath(QStringLiteral("chevron_right"), IconColor::Accent, 12);
    const QString chevronOpenSelected = chevronAssetPath(QStringLiteral("expand_more"), IconColor::Accent, 12);

    return QStringLiteral("QTreeView {"
                          "    background: %1;"
                          "    border: none;"
                          "    outline: 0;"
                          // NavItemDelegate::paint() already fully draws the selected row's
                          // background/text itself, but Fusion separately paints its own
                          // native "current item" indicator using QPalette::Highlight/
                          // HighlightedText, which QSS never touched — left at its default,
                          // that renders as a stray blue sliver over our surface-400 fill.
                          // selection-background-color/selection-color map straight onto
                          // those two palette roles, so matching them to the same tone the
                          // delegate already paints makes that native decoration blend in
                          // instead of standing out as a different color.
                          "    selection-background-color: %6;"
                          "    selection-color: %7;"
                          "}"
                          // Explicit width/height, not just image: — without a fixed box,
                          // Fusion sizes the branch indicator (and the gap it reserves
                          // before the row's text) from the image's own pixel dimensions,
                          // so chevronAssetPath's 4x-oversampled source (needed for
                          // crispness at a fractional display scale, see Icons.cpp) blew
                          // the indicator up to 4x its intended 12px size instead of just
                          // supersampling it. Pinning the box to 12px makes Qt scale the
                          // oversampled image down into it instead.
                          "QTreeView::branch:closed:has-children { image: url(\"%2\"); width: 12px; height: 12px; }"
                          "QTreeView::branch:open:has-children { image: url(\"%3\"); width: 12px; height: 12px; }"
                          "QTreeView::branch:selected:closed:has-children {"
                          "    image: url(\"%4\"); width: 12px; height: 12px;"
                          "}"
                          "QTreeView::branch:selected:open:has-children {"
                          "    image: url(\"%5\"); width: 12px; height: 12px;"
                          "}")
        .arg(hex(p.surface100), chevronClosed, chevronOpen, chevronClosedSelected, chevronOpenSelected)
        .arg(hex(p.surface400), hex(p.accent));
}

} // namespace

QString buildStyleSheet(Mode mode)
{
    const Palette& p = palette(mode);
    return buttonsBlock(p) + textButtonsBlock(p) + slidersBlock(p) + scrollbarBlock(p) + toolBarBlock(p)
        + panelsBlock(p) + progressBarBlock(p) + sidebarTreeBlock(p);
}

void applyGlobalStyleSheet(QApplication& app)
{
    app.setStyleSheet(buildStyleSheet(currentMode()));
    QObject::connect(
        &notifier(), &Notifier::changed, &app, [&app]() { app.setStyleSheet(buildStyleSheet(currentMode())); });
}

} // namespace Theme
