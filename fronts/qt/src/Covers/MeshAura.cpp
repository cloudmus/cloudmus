#include "MeshAura.h"

#include <cmath>

#include <QColor>
#include <QDate>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

namespace Covers {

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

// A box blur along one axis: from `source` lines into `target` columns, so
// that two passes make both axes (and flip the image back).
void boxBlurInto(const QImage& source, QImage& target, int radius)
{
    const int width = source.width();
    const int height = source.height();
    const int window = 2 * radius + 1;
    for (int y = 0; y < height; ++y) {
        const auto* in = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        int sums[4] = { 0, 0, 0, 0 };
        const auto add = [&](QRgb px, int sign) {
            sums[0] += sign * qAlpha(px);
            sums[1] += sign * qRed(px);
            sums[2] += sign * qGreen(px);
            sums[3] += sign * qBlue(px);
        };
        for (int x = -radius; x <= radius; ++x)
            add(in[qBound(0, x, width - 1)], 1);
        for (int x = 0; x < width; ++x) {
            reinterpret_cast<QRgb*>(target.scanLine(x))[y]
                = qRgba(sums[1] / window, sums[2] / window, sums[3] / window, sums[0] / window);
            add(in[qMin(x + radius + 1, width - 1)], 1);
            add(in[qMax(x - radius, 0)], -1);
        }
    }
}

// Shared by the image and whoever fades its edges, so the two always match.
constexpr qreal kBlurRadius = 45.0;

} // namespace

qreal meshAuraBlurRadius() { return kBlurRadius; }

QImage gaussianBlur(const QImage& source, qreal radius)
{
    // A Qt blur of radius r has about r/2 for its sigma; three boxes of
    // radius b have about b * sqrt(2) for theirs.
    const int boxRadius = qMax(1, qRound(radius / 2.0 / std::sqrt(2.0)));
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage turned(image.height(), image.width(), QImage::Format_ARGB32_Premultiplied);
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurInto(image, turned, boxRadius);
        boxBlurInto(turned, image, boxRadius);
    }
    return image;
}

QImage meshAuraImage(const QString& title, const QSize& size, const Blur& blur, qreal renderScale)
{
    const int fullWidth = qMax(size.width(), 1);
    const int fullHeight = qMax(size.height(), 1);
    const int w = qMax(qRound(fullWidth * renderScale), 1);
    const int h = qMax(qRound(fullHeight * renderScale), 1);
    const qreal blurRadius = kBlurRadius * renderScale;

    // Today's date folded into the seed alongside the title: same title on
    // the same day always renders the same cover (still no need to persist
    // anything — see the header), but the palette/mesh drifts to a new one
    // the next day. ISODate ("2026-08-05") rather than e.g. a day-of-year
    // number so this doesn't quietly collide/repeat on next year's same day.
    const QString seedInput = title + QDate::currentDate().toString(Qt::ISODate);
    Mulberry32 rng(xmur3(seedInput));

    // Rendered with a margin on every side and cropped after the blur:
    // blurring right up to the image edge pulls in transparency from
    // outside it, leaving a semi-transparent fringe that lets whatever is
    // behind show through (and flicker when two of these are crossfaded).
    // With the margin, the result is opaque edge to edge.
    const int pad = qCeil(blurRadius * 2.0);
    QImage image(w + 2 * pad, h + 2 * pad, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const int baseHue = static_cast<int>(rng.next() * 360.0);
    const QColor baseColor = QColor::fromHslF(static_cast<float>(baseHue) / 360.0f, 0.50f, 0.12f);
    painter.fillRect(image.rect(), baseColor);

    // Five soft, alpha-fading radial-gradient blobs, blended over one
    // another and the background via QPainter's default SourceOver — same
    // effect as the canvas original's default globalCompositeOperation.
    // The 180-260px blob radii below are tuned against the original's 600x600
    // square canvas; on a wide, short strip, unscaled, they'd engulf all of it
    // several times over and wash it out to one color. Scaling by sqrt(w*h)
    // (geometric mean, ==600 for the original, so scale==1 there) rather than
    // min or max: min alone shrinks blobs enough that 5 of them leave most of
    // a strip empty; max alone makes the washing out worse. cx/cy don't need
    // touching — they already scale naturally (rng()*w, rng()*h).
    const qreal scale = std::sqrt(static_cast<qreal>(w) * static_cast<qreal>(h)) / 600.0;

    painter.setPen(Qt::NoPen);
    painter.translate(pad, pad);
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

    // The blur's faint tails still reach a little past the margin; laying
    // the result over the base color makes it strictly opaque.
    QImage blurred = blur(image, blurRadius).copy(pad, pad, w, h);
    if (w != fullWidth || h != fullHeight)
        blurred = blurred.scaled(fullWidth, fullHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = QImage(fullWidth, fullHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(baseColor);
    QPainter(&image).drawImage(0, 0, blurred);
    applyNoise(image, rng);
    return image;
}

} // namespace Covers
