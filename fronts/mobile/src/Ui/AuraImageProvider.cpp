#include "AuraImageProvider.h"

#include <QColor>
#include <QPainter>
#include <QStringList>
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
    const QSize target(requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide,
        requestedSize.height() > 0 ? requestedSize.height() : kDefaultSide);
    const qreal scale = qMin(1.0, qreal(kSmoothSide) / qMax(target.width(), target.height()));
    const QStringList parts = id.startsWith(QLatin1Char('h')) ? id.mid(1).split(QLatin1Char('/')) : QStringList();
    const bool hero = parts.size() == 3;
    const QString seed = QUrl::fromPercentEncoding((hero ? parts[2] : id.mid(1)).toUtf8());

    QImage image = Covers::meshAuraImage(seed, target, &Covers::gaussianBlur, scale);
    if (hero) {
        QPainter painter(&image);
        const qreal radius = parts[1].toDouble();
        if (radius > 0) {
            // Smooth as well: drawn small, like the art.
            const QSize small = (QSizeF(target) * scale).toSize().expandedTo(QSize(1, 1));
            const QColor color(QLatin1Char('#') + parts[0]);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(
                image.rect(), Covers::meshAuraEdgeFade(small, color, &Covers::gaussianBlur, radius * scale));
        }
        Covers::paintMeshAuraVignette(painter, image.rect());
    }
    if (size)
        *size = image.size();
    return image;
}

} // namespace Ui
