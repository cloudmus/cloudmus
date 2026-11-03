#pragma once

#include <QQuickImageProvider>

namespace Ui {

// `image://aura/s<percent-encoded seed>`: the desktop's Mesh Aura art, for QML
// (see Covers::meshAuraImage()). The `s` keeps the URL whole for an empty
// seed, which has an image of its own too. Drawn on a loader thread.
class AuraImageProvider : public QQuickImageProvider {
public:
    AuraImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Ui
