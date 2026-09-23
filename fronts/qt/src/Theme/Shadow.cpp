#include "Shadow.h"

#include <QPainter>
#include <QPainterPath>

namespace Theme {

void paintSoftShadow(QPainter* painter, const QRect& contentRect, int margin, int offsetY, int maxAlpha, int radius)
{
    painter->save();
    painter->setBrush(Qt::NoBrush);
    // Runs down past 0 into negative spreads: the rings are shifted down by
    // offsetY, so without these the strip right under the content's bottom
    // edge would get no ring at all and show as a visible unshadowed gap.
    // Elsewhere they sit under the (opaque) content, unseen.
    for (int spread = margin; spread >= 1 - offsetY; --spread) {
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
