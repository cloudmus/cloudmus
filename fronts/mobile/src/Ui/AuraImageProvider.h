#pragma once

#include <QQuickImageProvider>

namespace Ui {

// `image://aura/s<percent-encoded seed>`: the desktop's Mesh Aura art, for QML
// (see Covers::meshAuraImage()). The `s` keeps the URL whole for an empty
// seed, which has an image of its own too.
//
// `image://aura/h<edge color's RRGGBB>/<edge fade's radius in pixels>/<seed>`:
// the same as the desktop's hero has it, with its edges dissolving into that
// color (none at radius 0) and its vignette, all in one image: nothing to
// composite per frame. Drawn on a loader thread.
class AuraImageProvider : public QQuickImageProvider {
public:
    AuraImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Ui
