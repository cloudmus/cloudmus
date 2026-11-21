#pragma once

#include <QQuickImageProvider>

namespace Ui {

// `image://cover/<radius in pixels>/f<percent-encoded file URL>`, or
// `.../s<percent-encoded seed>` for generated art: a cover, cut square, with
// rounded corners in its own transparency. Drawn once on a loader thread
// rather than masked per frame, so that a long list of them scrolls for free.
class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Ui
