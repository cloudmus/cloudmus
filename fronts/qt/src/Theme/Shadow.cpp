#include "Shadow.h"

#include <QPainter>
#include <QPainterPath>

namespace Theme {

void paintSoftShadow(QPainter* painter, const QRect& contentRect, int margin, int offsetY, int maxAlpha, int radius)
{
    if (margin <= 0)
        return; // no room reserved: the window system's shadow instead
    painter->save();
    painter->setBrush(Qt::NoBrush);
    // Only outside the content: glass content is translucent, and the
    // rings under its edge — the darkest ones — showed through it as a
    // dark band a few pixels wide just inside the border.
    QPainterPath outside;
    const int reach = margin + qAbs(offsetY) + 2; // past the outermost ring's pen
    outside.addRect(QRectF(contentRect.adjusted(-reach, -reach, reach, reach)));
    QPainterPath content;
    content.addRoundedRect(QRectF(contentRect), radius, radius);
    painter->setClipPath(outside.subtracted(content), Qt::IntersectClip);
    // Runs down past 0 into negative spreads: the rings are shifted down by
    // offsetY, so without these the strip right under the content's bottom
    // edge would get no ring at all and show as a visible unshadowed gap.
    // Down to -offsetY, not one short of it: every pixel row sits under two
    // of these 2px rings, and with the last one missing, the row right
    // under the bottom edge got only one — a 1px lighter line there.
    for (int spread = margin; spread >= -offsetY; --spread) {
        const qreal t = qreal(qMax(spread, 0)) / margin; // 1 at the outer edge, 0 at the content
        const int alpha = qRound(maxAlpha * (1.0 - t) * (1.0 - t));
        if (alpha <= 0)
            continue;
        const QRect layerRect = contentRect.adjusted(-spread, -spread, spread, spread).translated(0, offsetY);
        // Width 2, one step apart: a 1px gap between consecutive rings
        // would show as faint seams; this overlaps them by ~1px instead.
        painter->setPen(QPen(QColor(0, 0, 0, alpha), 2));
        QPainterPath path;
        path.addRoundedRect(QRectF(layerRect), radius + spread, radius + spread);
        painter->drawPath(path);
    }
    painter->restore();
}

} // namespace Theme
