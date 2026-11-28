#include "GeneratedCoverArt.h"

#include <QColor>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>

#include "CoverPicture.h"
#include "MeshAura.h"

namespace Ui {

namespace {

// CSS's filter: blur(45px) is a true Gaussian blur; QPainter has no direct
// equivalent, so this renders through a throwaway QGraphicsScene with a
// QGraphicsBlurEffect instead — the standard way to get one in Qt Widgets
// without hand-rolling a convolution kernel. Offscreen (no window needed):
// scene.render() just paints into whatever QPainter/device you give it.
QImage blurImage(const QImage& src, qreal radius)
{
    QGraphicsScene scene;
    QGraphicsPixmapItem* item = scene.addPixmap(QPixmap::fromImage(src));
    auto* blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(radius);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(blur);

    QImage result(src.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(0, 0, src.width(), src.height()), QRectF(0, 0, src.width(), src.height()));
    painter.end();
    return result;
}

} // namespace

QPixmap generateMeshAuraGradientCover(const QString& title, const QSize& size)
{
    return QPixmap::fromImage(Covers::meshAuraImage(title, size, &blurImage));
}

QPixmap generateMeshAuraEdgeFade(const QSize& size, const QColor& color)
{
    return QPixmap::fromImage(Covers::meshAuraEdgeFade(size, color, &blurImage));
}

QPixmap fitCover(const QImage& source, const QSize& target)
{
    return QPixmap::fromImage(Covers::fitCover(source, target, &blurImage));
}

} // namespace Ui
