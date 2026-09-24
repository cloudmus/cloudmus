#pragma once

#include <QFont>

class QWidget;

namespace Theme {

enum class TextStyle {
    Display, // 22px/700 — HeroPanel title
    Title, // 14px/600 — track/playlist names, sidebar source names
    Body, // 13px/500 — default UI text
    BodySecondary, // 12px/400 — secondary text under a title (artist, counts)
    Caption, // 11px/500 — timecodes, durations, small labels
    // 11px/700, letter-spacing 0.06em — sidebar section headers. Uppercasing
    // is the caller's job (QString::toUpper() at text-assignment time, not a
    // paint-time transform) — see the design system's README.
    LabelUpper,
    Button, // 13px/600 — button labels, actionable menu items
};

// Builds a QFont for the given style: family "Manrope" (see Fonts.h — must
// be registered before the first call), design-system pixel size and
// weight, on top of `base` (so DPI/style-dependent fallbacks it already
// carries aren't lost). Manrope's static weight files (see resources/fonts)
// all register under the one family name "Manrope" (confirmed via
// fc-query), so a single setFamily("Manrope") + setWeight() reliably
// resolves to the corresponding weight file through Qt's own font matching.
QFont font(TextStyle style, const QFont& base = QFont());

// Same as font(), plus tabular (monospaced) figures via the "tnum" OpenType
// feature — for timecodes/durations, where digits must not shift width as
// they change. Not the default for every style: QFont::setFeature is Qt
// 6.7+ per-QFont API, applied only where digits actually need to stay
// aligned.
QFont tabularFont(TextStyle style, const QFont& base = QFont());

// Explicitly opts a widget OUT of the Manrope default QApplication::setFont()
// establishes app-wide (see main.cpp) — for a native/system dialog
// (QFileDialog, QColorDialog, QPrintDialog, ...) that should keep the
// platform's own font instead of ours. Qt's font inheritance follows the
// widget parent-child tree with no "stop here" boundary a dialog or window
// can raise, and QApplication::setFont()'s default applies with no subtree
// opt-out either — so restoring the real platform font on the dialog
// instance itself is the only way back. Call this on every native dialog
// this app constructs itself (right after construction, before exec());
// see StyleSheet.cpp's file-level comment for the equivalent QSS-side rule.
void useSystemFont(QWidget* widget);

} // namespace Theme
