#pragma once

#include <QRect>

class QPainter;

namespace Theme {

// A soft drop shadow hand-painted around `contentRect` into a margin the
// caller reserved around it — for popups (QMenu, tooltips, hover cards)
// that are top-level windows: a QGraphicsDropShadowEffect's blur bleeds
// past the widget's own geometry, and a top-level window can't paint
// beyond its own frame, so the effect's blur just gets clipped away.
//
// Concentric rounded-rect OUTLINES, not filled shapes: filled, overlapping
// rounded rects share a center, so every pixel near the edge sits inside
// all of them and SourceOver compounds their alpha (1-(1-a)^n) far past
// any single layer's — that compounding made the shadow look much too
// dark. A thin ring per step touches each pixel about once.
//
// `margin` is the reserved width (the shadow's reach), `offsetY` shifts it
// down like a light from above, `maxAlpha` is the darkness right at the
// content's edge, `radius` the content's corner radius.
void paintSoftShadow(QPainter* painter, const QRect& contentRect, int margin, int offsetY, int maxAlpha, int radius);

} // namespace Theme
