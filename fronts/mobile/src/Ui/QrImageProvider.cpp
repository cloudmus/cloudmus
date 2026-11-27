#include "QrImageProvider.h"

#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include <algorithm>

#include "qrcodegen.hpp"

namespace Ui {

namespace {
// The blank margin a scanner needs around the code, in modules.
constexpr int kQuietZone = 3;
constexpr int kDefaultSide = 256;
// A finder pattern, the square in three of the corners, in modules.
constexpr int kFinderSide = 7;
// A dot's diameter, in modules: much smaller and scanners lose the grid.
constexpr qreal kDot = 0.9;

bool inFinder(int x, int y, int side)
{
    const bool left = x < kFinderSide;
    const bool top = y < kFinderSide;
    return (left && top) || (left && y >= side - kFinderSide) || (top && x >= side - kFinderSide);
}

// A rounded ring around a rounded square; `at` is its corner, in modules.
void paintFinder(QPainter& painter, const QPointF& at)
{
    QPainterPath ring;
    ring.addRoundedRect(QRectF(at, QSizeF(kFinderSide, kFinderSide)), 2.2, 2.2);
    ring.addRoundedRect(QRectF(at + QPointF(1, 1), QSizeF(5, 5)), 1.4, 1.4);
    ring.setFillRule(Qt::OddEvenFill);
    painter.drawPath(ring);
    painter.drawRoundedRect(QRectF(at + QPointF(2, 2), QSizeF(3, 3)), 0.9, 0.9);
}
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
    // At least a pixel per module.
    const int side = std::max(asked, modules);
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    // From here on a unit is a module.
    painter.scale(qreal(side) / modules, qreal(side) / modules);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(0, 0, modules, modules), kQuietZone, kQuietZone);
    painter.translate(kQuietZone, kQuietZone);
    painter.setBrush(Qt::black);
    for (int y = 0; y < code.getSize(); ++y) {
        for (int x = 0; x < code.getSize(); ++x) {
            if (code.getModule(x, y) && !inFinder(x, y, code.getSize()))
                painter.drawEllipse(QPointF(x + 0.5, y + 0.5), kDot / 2, kDot / 2);
        }
    }
    const int far = code.getSize() - kFinderSide;
    for (const QPoint& corner : { QPoint(0, 0), QPoint(far, 0), QPoint(0, far) })
        paintFinder(painter, corner);
    painter.end();
    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace Ui
