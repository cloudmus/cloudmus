#include "Transition.h"

#include <QPainter>

namespace Ui {

LayerState lerp(const LayerState& from, const LayerState& to, qreal t)
{
    return { from.opacity + (to.opacity - from.opacity) * t, from.scale + (to.scale - from.scale) * t };
}

void applyLayerState(QPainter& painter, const LayerState& state, const QPointF& origin)
{
    painter.setOpacity(painter.opacity() * state.opacity);
    if (state.scale != 1.0) {
        painter.translate(origin);
        painter.scale(state.scale, state.scale);
        painter.translate(-origin);
    }
}

} // namespace Ui
