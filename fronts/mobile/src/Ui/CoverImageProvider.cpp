#include "CoverImageProvider.h"

#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include "CoverPicture.h"
#include "MeshAura.h"

namespace Ui {

namespace {

constexpr int kDefaultSide = 256;

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
    // What's blurred is drawn no larger than this: it looks the same, sooner.
    const qreal blurScale = qMin(1.0, 480.0 / side);

    QImage source;
    if (kind == QLatin1Char('s'))
        source = Covers::meshAuraImage(value, QSize(side, side), &Covers::gaussianBlur, blurScale);
    else
        source = Covers::readScaled(QUrl(value).toLocalFile(), side);
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
    painter.drawImage(0, 0, Covers::fitCover(source, rounded.size(), &Covers::gaussianBlur, blurScale));
    painter.end();

    if (size)
        *size = rounded.size();
    return rounded;
}

} // namespace Ui
