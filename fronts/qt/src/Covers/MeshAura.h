#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>

class QPainter;

namespace Covers {

// Deterministic "Mesh Aura Gradient" art, generated from a title plus today's
// date: the same title on the same day always gives the same image (nothing
// to persist), and the palette drifts to a new one tomorrow. Ported from the
// "1. Mesh Aura Gradient" canvas example: five blurred, alpha-fading radial
// blobs over a dark hue-shifted background, plus a light grain pass. Opaque
// edge to edge.
//
// For a front that has a Gaussian blur of its own (Qt Widgets' graphics
// effects) `blur` is that; without one, gaussianBlur() below.
using Blur = std::function<QImage(const QImage& source, qreal radius)>;

// `renderScale` < 1 draws the smooth part smaller and scales it up, for a
// large image in a hurry; the grain stays at full size.
QImage meshAuraImage(const QString& title, const QSize& size, const Blur& blur, qreal renderScale = 1.0);

// Three box blurs in a row, as near to a Gaussian as the eye can tell here.
QImage gaussianBlur(const QImage& source, qreal radius);

// The blur's radius on a `meshAuraImage()`: what is to be left around it for
// the blur not to pull transparency in at the edges.
qreal meshAuraBlurRadius();

// `color` fading in toward the edges of a `size` rect with a Gaussian falloff
// of `radius`: exactly what blurring an image right up to its edges would have
// let show through from a `color` backdrop. Painted over the art, the two look
// like it softly dissolving into `color` at the edges.
QImage meshAuraEdgeFade(const QSize& size, const QColor& color, const Blur& blur, qreal radius = meshAuraBlurRadius());

// What keeps text legible over the art: dark at the center, where the text
// sits, fading out toward the edges. Painted over everything else.
void paintMeshAuraVignette(QPainter& painter, const QRect& rect);

} // namespace Covers
