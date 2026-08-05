#pragma once

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
QPixmap generateMeshAuraGradientCover(const QString& title, const QSize& size);

} // namespace Ui
