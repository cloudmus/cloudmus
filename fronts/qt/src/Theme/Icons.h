#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace Theme {

// The design-token colors an icon glyph can be recolored to. QSS/currentColor
// don't work on SVG icon *content* in Qt — only a real pixel-level recolor
// (see icon()'s implementation) produces the right color per state.
enum class IconColor {
    InkSecondary,
    Ink,
    Accent,
    InkTertiary,
    OnAccent
};

// Loads ":/icons/symbols/<name>.svg" (see resources/icons.qrc — classic
// Material Icons, Rounded/filled style, 24px) and returns it tinted to the
// given design token at pixelSize (device-pixel-ratio aware).
// Cached by (name, color, pixelSize, dpr, light/dark mode); the cache is
// cleared on Tokens::notifier().changed() since a mode flip changes which
// QColor a given IconColor resolves to.
QIcon icon(const QString& name, IconColor color, int pixelSize = 16);

// Same recolor, but for the handful of call sites (CoverPlaceholder's tone
// tiles) that need a specific Palette color IconColor has no semantic slot
// for (e.g. surface-100 as a glyph color) rather than one of the 5 named
// icon states above. Cached by the literal color value, not by theme mode —
// the caller is expected to have already resolved the color against the
// current Palette.
QIcon iconWithColor(const QString& name, const QColor& color, int pixelSize = 16);

// Same recoloring as icon(), but writes the result to a small on-disk PNG
// cache and returns its absolute path — needed because QSS's
// `QTreeView::branch { image: url(...); }` requires a real file, not an
// in-memory QPixmap. `glyphName` is "chevron_right" (collapsed) or
// "expand_more" (expanded) — the sidebar's two branch states. Regenerated
// once per (glyphName, color, pixelSize, mode); reused after.
QString chevronAssetPath(const QString& glyphName, IconColor color, int pixelSize = 12);

} // namespace Theme
