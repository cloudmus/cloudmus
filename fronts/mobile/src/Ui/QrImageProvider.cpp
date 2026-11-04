#include "QrImageProvider.h"

#include <QPainter>
#include <QUrl>

#include <algorithm>

#include "qrcodegen.hpp"

namespace Ui {

namespace {
// The blank margin a scanner needs around the code, in modules.
constexpr int kQuietZone = 2;
constexpr int kDefaultSide = 256;
}

QrImageProvider::QrImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    using qrcodegen::QrCode;
    const QByteArray text = QUrl::fromPercentEncoding(id.toUtf8()).toUtf8();
    const QrCode code = QrCode::encodeText(text.constData(), QrCode::Ecc::MEDIUM);
    const int modules = code.getSize() + kQuietZone * 2;
    const int asked = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide;
    // A whole number of pixels per module: blurred edges don't scan as well.
    const int perModule = std::max(1, asked / modules);
    const int side = perModule * modules;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < code.getSize(); ++y) {
        for (int x = 0; x < code.getSize(); ++x) {
            if (code.getModule(x, y)) {
                painter.fillRect((x + kQuietZone) * perModule, (y + kQuietZone) * perModule, perModule, perModule,
                    Qt::black);
            }
        }
    }
    painter.end();
    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace Ui
