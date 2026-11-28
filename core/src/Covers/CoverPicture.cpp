#include "CoverPicture.h"

#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>
#include <QtMath>

namespace Covers {

namespace {
Q_LOGGING_CATEGORY(lcCovers, "cloudmus.covers")
} // namespace

QImage readScaled(const QString& path, int side)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (side > 0 && original.isValid() && (original.width() > side && original.height() > side))
        reader.setScaledSize(original.scaled(side, side, Qt::KeepAspectRatioByExpanding));
    QImage image = reader.read();
    if (image.isNull())
        qCWarning(lcCovers) << "can't read the cover" << path << reader.errorString();
    return image;
}

QImage fitCover(const QImage& source, const QSize& target, const Blur& blur, qreal renderScale)
{
    if (source.isNull() || target.isEmpty())
        return { };
    const qreal sourceAspect = qreal(source.width()) / source.height();
    const qreal targetAspect = qreal(target.width()) / target.height();
    if (qAbs(sourceAspect / targetAspect - 1.0) < 0.02)
        return source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Background: the cover filling the whole target, blurred. Rendered
    // with a margin and cropped after, like the mesh cover, so the blur
    // doesn't pull transparency in at the edges.
    const QSize small(qMax(qRound(target.width() * renderScale), 1), qMax(qRound(target.height() * renderScale), 1));
    const qreal radius = qMax(4.0 * renderScale, small.width() / 10.0);
    const int pad = qCeil(radius * 2.0);
    const QSize padded = small + QSize(2 * pad, 2 * pad);
    QImage fill = source.scaled(padded, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    fill = fill.copy((fill.width() - padded.width()) / 2, (fill.height() - padded.height()) / 2, padded.width(),
                   padded.height())
               .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage background = blur(fill, radius).copy(pad, pad, small.width(), small.height());

    QImage out(target, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(out.rect(), background);
    painter.fillRect(out.rect(), QColor(0, 0, 0, 60)); // so the real cover stands out from its blur
    const QImage fitted = source.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2, fitted);
    painter.end();
    return out;
}

} // namespace Covers
