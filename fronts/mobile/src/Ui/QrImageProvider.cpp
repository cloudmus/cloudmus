#include "QrImageProvider.h"

#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include <algorithm>
#include <array>

#include "qrcodegen.hpp"

namespace Ui {

namespace {
// The blank margin a scanner needs around the code, in modules.
constexpr int kQuietZone = 3;
constexpr int kDefaultSide = 256;
// A finder pattern, the square in three of the corners, in modules.
constexpr int kFinderSide = 7;
// How round a module's corners are, in modules: half a module, so that one
// on its own is a dot and the end of a run is a half circle.
constexpr qreal kRound = 0.5;

bool inFinder(int x, int y, int side)
{
    const bool left = x < kFinderSide;
    const bool top = y < kFinderSide;
    return (left && top) || (left && y >= side - kFinderSide) || (top && x >= side - kFinderSide);
}

// The module at `at`, square where it runs into a neighbour; `corners` says
// which are round, clockwise from the top left.
void addModule(QPainterPath& path, const QPointF& at, const std::array<bool, 4>& corners)
{
    const QRectF cell(at, QSizeF(1, 1));
    const std::array<QPointF, 4> points { cell.topLeft(), cell.topRight(), cell.bottomRight(), cell.bottomLeft() };
    // From each corner towards the middle, to its arc's center.
    const std::array<QPointF, 4> inwards { QPointF(1, 1), QPointF(-1, 1), QPointF(-1, -1), QPointF(1, -1) };
    for (int i = 0; i < 4; ++i) {
        if (!corners[i]) {
            i == 0 ? path.moveTo(points[i]) : path.lineTo(points[i]);
            continue;
        }
        const QPointF center = points[i] + inwards[i] * kRound;
        const QRectF circle(center - QPointF(kRound, kRound), QSizeF(kRound * 2, kRound * 2));
        // Qt's angles go counterclockwise from three o'clock; these go round
        // the module clockwise.
        const qreal from = 180 - 90 * i;
        if (i == 0)
            path.arcMoveTo(circle, from);
        path.arcTo(circle, from, -90);
    }
    path.closeSubpath();
}

// The inner corner of a bend: what a light module has filled in at `corner`,
// `inwards` pointing from there to its middle.
void addFillet(QPainterPath& path, const QPointF& corner, const QPointF& inwards)
{
    const QPointF alongX(corner.x() + inwards.x() * kRound, corner.y());
    const QPointF alongY(corner.x(), corner.y() + inwards.y() * kRound);
    // A quarter circle bulging towards the corner, as a Bezier curve.
    constexpr qreal kQuarter = 0.5523;
    // Wound the same way as the modules, or their shared edges would cancel.
    const bool clockwise = inwards.x() * inwards.y() > 0;
    const QPointF first = clockwise ? alongX : alongY;
    const QPointF second = clockwise ? alongY : alongX;
    path.moveTo(corner);
    path.lineTo(first);
    path.cubicTo(first + (corner - first) * kQuarter, second + (corner - second) * kQuarter, second);
    path.closeSubpath();
}

// The data modules as one shape: neighbours flow together, outer corners are
// round and so are the inner ones of every bend. Modules touching only
// diagonally stay apart.
QPainterPath dataShape(const qrcodegen::QrCode& code)
{
    const int side = code.getSize();
    // The finder patterns are painted on their own.
    const auto dark = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < side && y < side && code.getModule(x, y) && !inFinder(x, y, side);
    };
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const bool left = dark(x - 1, y);
            const bool right = dark(x + 1, y);
            const bool up = dark(x, y - 1);
            const bool down = dark(x, y + 1);
            if (dark(x, y)) {
                addModule(path, QPointF(x, y), { !left && !up, !right && !up, !right && !down, !left && !down });
                continue;
            }
            if (inFinder(x, y, side))
                continue;
            if (left && up && dark(x - 1, y - 1))
                addFillet(path, QPointF(x, y), QPointF(1, 1));
            if (right && up && dark(x + 1, y - 1))
                addFillet(path, QPointF(x + 1, y), QPointF(-1, 1));
            if (right && down && dark(x + 1, y + 1))
                addFillet(path, QPointF(x + 1, y + 1), QPointF(-1, -1));
            if (left && down && dark(x - 1, y + 1))
                addFillet(path, QPointF(x, y + 1), QPointF(1, -1));
        }
    }
    return path;
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
    painter.drawPath(dataShape(code));
    const int far = code.getSize() - kFinderSide;
    for (const QPoint& corner : { QPoint(0, 0), QPoint(far, 0), QPoint(0, far) })
        paintFinder(painter, corner);
    painter.end();
    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace Ui
