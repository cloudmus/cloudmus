#pragma once

#include <QString>

class QPainter;
class QRect;

namespace Theme {

// Paints the CloudMus design system's "no artwork" tile into `rect`:
// - No genre tag: one of 3 fixed tone/glyph-color pairs, picked
//   deterministically from `stableId` (same id -> same tone, always — see
//   the .cpp for why this must not be random per repaint).
// - Genre tag given: a muted tile with a small centered caption instead of
//   a glyph.
// `stableId` should be unique across the whole app, not just within one
// source (e.g. sourceId + trackId), since track ids aren't guaranteed
// globally unique.
void paintCoverPlaceholder(
    QPainter* painter, const QRect& rect, const QString& stableId, const QString& genreTag = QString());

} // namespace Theme
