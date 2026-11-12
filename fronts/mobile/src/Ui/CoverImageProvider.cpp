#include "CoverImageProvider.h"

#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include "MeshAura.h"

namespace Ui {

namespace {

constexpr int kDefaultSide = 256;

// Scaled to fill `side`, read no larger than it needs to be.
QImage readFile(const QString& path, int side)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid())
        reader.setScaledSize(original.scaled(side, side, Qt::KeepAspectRatioByExpanding));
    return reader.read();
}

} // namespace

CoverImageProvider::CoverImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{
}

QImage CoverImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const int separator = id.indexOf(QLatin1Char('/'));
    if (separator <= 0 || separator + 1 >= id.size())
        return { };
    const qreal radius = id.left(separator).toDouble();
    const QChar kind = id.at(separator + 1);
    const QString value = QUrl::fromPercentEncoding(id.mid(separator + 2).toUtf8());
    const int side = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide;

    QImage source;
    if (kind == QLatin1Char('s'))
        source = Covers::meshAuraImage(value, QSize(side, side), &Covers::gaussianBlur, qMin(1.0, 480.0 / side));
    else
        source = readFile(QUrl(value).toLocalFile(), side);
    if (source.isNull())
        return { };

    QImage rounded(side, side, QImage::Format_ARGB32_Premultiplied);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rounded.rect()), radius, radius);
    painter.setClipPath(clip);
    const QSize filled = source.size().scaled(side, side, Qt::KeepAspectRatioByExpanding);
    painter.drawImage(QRect(QPoint((side - filled.width()) / 2, (side - filled.height()) / 2), filled), source);
    painter.end();

    if (size)
        *size = rounded.size();
    return rounded;
}

} // namespace Ui
