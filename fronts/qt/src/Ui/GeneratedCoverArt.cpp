#include "GeneratedCoverArt.h"

#include <QColor>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QtMath>

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

// Shared by the cover and its edge fade so the two always match.
const qreal kBlurRadius = Covers::meshAuraBlurRadius();
const int kBlurPad = qCeil(kBlurRadius * 2.0);

} // namespace

QPixmap generateMeshAuraGradientCover(const QString& title, const QSize& size)
{
    return QPixmap::fromImage(Covers::meshAuraImage(title, size, &blurImage));
}

QPixmap generateMeshAuraEdgeFade(const QSize& size, const QColor& color)
{
    const int w = qMax(size.width(), 1);
    const int h = qMax(size.height(), 1);

    // An opaque rect blurred against transparency around it: its alpha is
    // the coverage the cover itself would have had near its edges.
    QImage coverage(w + 2 * kBlurPad, h + 2 * kBlurPad, QImage::Format_ARGB32_Premultiplied);
    coverage.fill(Qt::transparent);
    QPainter(&coverage).fillRect(kBlurPad, kBlurPad, w, h, Qt::white);
    coverage = blurImage(coverage, kBlurRadius).copy(kBlurPad, kBlurPad, w, h);

    // Whatever the cover doesn't cover, `color` shows through.
    QImage fade(w, h, QImage::Format_ARGB32_Premultiplied);
    fade.fill(color);
    QPainter painter(&fade);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.drawImage(0, 0, coverage);
    painter.end();
    return QPixmap::fromImage(fade);
}

QPixmap fitCover(const QImage& source, const QSize& target)
{
    if (source.isNull() || target.isEmpty())
        return { };
    const qreal sourceAspect = qreal(source.width()) / source.height();
    const qreal targetAspect = qreal(target.width()) / target.height();
    if (qAbs(sourceAspect / targetAspect - 1.0) < 0.02)
        return QPixmap::fromImage(source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

    // Background: the cover filling the whole target, blurred. Rendered
    // with a margin and cropped after, like the mesh cover, so the blur
    // doesn't pull transparency in at the edges.
    const qreal radius = qMax(4.0, target.width() / 10.0);
    const int pad = qCeil(radius * 2.0);
    const QSize padded = target + QSize(2 * pad, 2 * pad);
    QImage fill = source.scaled(padded, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    fill = fill.copy((fill.width() - padded.width()) / 2, (fill.height() - padded.height()) / 2, padded.width(),
                   padded.height())
               .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage background = blurImage(fill, radius).copy(pad, pad, target.width(), target.height());

    QImage out(target, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(0, 0, background);
    painter.fillRect(out.rect(), QColor(0, 0, 0, 60)); // so the real cover stands out from its blur
    const QImage fitted = source.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2, fitted);
    painter.end();
    return QPixmap::fromImage(out);
}

} // namespace Ui
