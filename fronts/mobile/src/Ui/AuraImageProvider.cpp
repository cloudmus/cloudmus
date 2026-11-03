#include "AuraImageProvider.h"

#include <QUrl>

#include "MeshAura.h"

namespace Ui {

namespace {
constexpr int kDefaultSide = 512;
// Its blobs and blur are smooth: no point drawing them larger than this.
constexpr int kSmoothSide = 480;
}

AuraImageProvider::AuraImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{
}

QImage AuraImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString seed = QUrl::fromPercentEncoding(id.mid(1).toUtf8());
    const QSize target(requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide,
        requestedSize.height() > 0 ? requestedSize.height() : kDefaultSide);
    const qreal scale = qMin(1.0, qreal(kSmoothSide) / qMax(target.width(), target.height()));
    const QImage image = Covers::meshAuraImage(seed, target, &Covers::gaussianBlur, scale);
    if (size)
        *size = image.size();
    return image;
}

} // namespace Ui
