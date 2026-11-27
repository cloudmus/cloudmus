#include "QrImageProvider.h"

#include <QUrl>

#include "QrPicture.h"

namespace Ui {

namespace {
constexpr int kDefaultSide = 256;
}

QrImageProvider::QrImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QImage image = Qr::picture(
        QUrl::fromPercentEncoding(id.toUtf8()), requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide);
    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace Ui
