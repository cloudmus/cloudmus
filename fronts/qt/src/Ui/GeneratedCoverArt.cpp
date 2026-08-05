#include "GeneratedCoverArt.h"

#include <cmath>

#include <QColor>
#include <QDate>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QRadialGradient>

namespace Ui {

namespace {

// xmur3 (32-bit string hash) + mulberry32 (fast deterministic PRNG),
// ported bit-for-bit from playlist_covers_showcase.html: same operations,
// same constants, uint32_t wraparound standing in for JS's Math.imul/>>>
// (identical low-32-bit results either way). QString::size()/QChar::unicode()
// match JS's str.length/charCodeAt() exactly for any string within the
// Unicode BMP — including Cyrillic playlist titles.
quint32 xmur3(const QString& str)
{
    quint32 h = 1779033703u ^ static_cast<quint32>(str.size());
    for (const QChar ch : str) {
        h ^= static_cast<quint32>(ch.unicode());
        h *= 3432918353u;
        h = (h << 13) | (h >> 19);
    }
    h = (h ^ (h >> 16)) * 2246822507u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h ^= h >> 16;
    return h;
}

class Mulberry32 {
public:
    explicit Mulberry32(quint32 seed)
        : a_(seed)
    {
    }

    // In [0, 1), matching the JS generator's return value exactly in spirit
    // (same recurrence, same constants).
    double next()
    {
        quint32 t = (a_ += 0x6D2B79F5u);
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }

private:
    quint32 a_;
};

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

// One shared noise value per pixel (not per channel — matches the JS loop,
// which reuses a single `noise` across data[i]/[i+1]/[i+2]), in [-7, 7).
void applyNoise(QImage& image, Mulberry32& rng)
{
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int noise = static_cast<int>((rng.next() - 0.5) * 14.0);
            const QRgb px = line[x];
            const int r = qBound(0, qRed(px) + noise, 255);
            const int g = qBound(0, qGreen(px) + noise, 255);
            const int b = qBound(0, qBlue(px) + noise, 255);
            line[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
}

} // namespace

QPixmap generateMeshAuraGradientCover(const QString& title, const QSize& size)
{
    const int w = qMax(size.width(), 1);
    const int h = qMax(size.height(), 1);

    // Today's date folded into the seed alongside the title: same title on
    // the same day always renders the same cover (still no need to persist
    // anything — see the header), but the palette/mesh drifts to a new one
    // the next day. ISODate ("2026-08-05") rather than e.g. a day-of-year
    // number so this doesn't quietly collide/repeat on next year's same day.
    const QString seedInput = title + QDate::currentDate().toString(Qt::ISODate);
    Mulberry32 rng(xmur3(seedInput));

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const int baseHue = static_cast<int>(rng.next() * 360.0);
    painter.fillRect(0, 0, w, h, QColor::fromHslF(static_cast<float>(baseHue) / 360.0f, 0.50f, 0.12f));

    // Five soft, alpha-fading radial-gradient blobs, blended over one
    // another and the background via QPainter's default SourceOver — same
    // effect as the canvas original's default globalCompositeOperation.
    // The 180-260px blob radii below are tuned against the 600x600 square
    // canvas the JS original always uses. PlaylistHeader's banner is
    // whatever aspect ratio the window gives it (a wide, short strip for a
    // browsable playlist; much taller for radioStation — see setPlaylist())
    // — applied unscaled, blobs sized for 600px routinely engulfed an entire
    // ~140px-tall banner several times over (top to bottom, several times
    // across), washing the whole thing out to one near-uniform color.
    // Scaling by sqrt(w*h) (geometric mean, ==600 for the original 600x600
    // case, so scale==1 there — no change) rather than min or max: min alone
    // shrinks blobs enough on a wide short strip that 5 of them leave most
    // of the width empty background instead of a blended mesh; max alone
    // makes the washed-out problem worse, not better. cx/cy don't need
    // touching — they already scale naturally (rng()*w, rng()*h).
    const qreal scale = std::sqrt(static_cast<qreal>(w) * static_cast<qreal>(h)) / 600.0;

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 5; ++i) {
        const qreal cx = rng.next() * w;
        const qreal cy = rng.next() * h;
        const qreal radius = (180.0 + rng.next() * 260.0) * scale;
        const qreal hue = std::fmod(baseHue + (rng.next() * 140.0 - 70.0) + 360.0, 360.0);

        QColor stopColor = QColor::fromHslF(static_cast<float>(hue / 360.0), 0.85f, 0.60f);
        QColor innerColor = stopColor;
        innerColor.setAlphaF(0.85f);
        QColor outerColor = stopColor;
        outerColor.setAlphaF(0.0f);

        QRadialGradient gradient(QPointF(cx, cy), radius);
        gradient.setColorAt(0.0, innerColor);
        gradient.setColorAt(1.0, outerColor);

        painter.setBrush(gradient);
        painter.drawEllipse(QPointF(cx, cy), radius, radius);
    }
    painter.end();

    image = blurImage(image, 45.0);
    applyNoise(image, rng);

    return QPixmap::fromImage(image);
}

} // namespace Ui
