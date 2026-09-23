#pragma once

#include <QColor>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace Ui {

// Deterministic "Mesh Aura Gradient" cover art, generated from a playlist's
// title plus today's date — same title + same day always produces the same
// image (no need to persist anything), but the palette drifts to a new one
// tomorrow. Stands in as PlaylistHeader's cover whenever a playlist has no
// real coverUrl. Ported from the "1. Mesh Aura Gradient" canvas example
// (playlist_covers_showcase.html): five blurred, alpha-fading radial-
// gradient blobs over a dark hue-shifted background, plus a light grain
// pass.
//
// Deliberately background-only — the JS original also draws its own title/
// subtitle onto the canvas (drawTypography), but PlaylistHeader already has
// its own title/description overlay (font, word-wrap, gradient scrim) and
// this is meant to slot into that unchanged, not duplicate it.
//
// Opaque edge to edge (see the .cpp) — the soft edge falloff lives in
// generateMeshAuraEdgeFade() instead, so a caller crossfading between
// two of these covers can keep that falloff static on top.
QPixmap generateMeshAuraGradientCover(const QString& title, const QSize& size);

// `color` fading in toward the edges of a `size` rect with the same
// Gaussian falloff the cover's blur has: exactly what blurring the cover
// right up to its edges would have let show through from a `color`
// backdrop. Painted over the cover, the two together look like the cover
// softly dissolving into `color` at the edges.
QPixmap generateMeshAuraEdgeFade(const QSize& size, const QColor& color);

} // namespace Ui
