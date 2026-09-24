#include "Icons.h"

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QScreen>
#include <QStandardPaths>
#include <QSvgRenderer>

#include "Tokens.h"

namespace Theme {

namespace {

QColor colorFor(IconColor color, const Palette& palette)
{
    switch (color) {
        case IconColor::InkSecondary:
            return palette.inkSecondary;
        case IconColor::Ink:
            return palette.ink;
        case IconColor::Accent:
            return palette.accent;
        case IconColor::InkTertiary:
            return palette.inkTertiary;
        case IconColor::OnAccent:
            return palette.onAccent;
    }
    return palette.ink;
}

qreal devicePixelRatio()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    return screen != nullptr ? screen->devicePixelRatio() : 1.0;
}

// The standard Qt SVG-recolor trick: render the (monochrome) glyph for its
// alpha shape, then flood the result with the target color using
// CompositionMode_SourceIn — currentColor doesn't work on SVGs painted via
// Qt, so this is the actual per-token-color recolor every icon needs.
QString glyphPath(const QString& glyphName) { return QStringLiteral(":/icons/symbols/%1.svg").arg(glyphName); }

QPixmap renderTinted(const QString& svgPath, const QColor& color, int pixelSize, qreal dpr)
{
    QSvgRenderer renderer(svgPath);
    const int devicePixels = qMax(1, qRound(pixelSize * dpr));
    QPixmap pixmap(devicePixels, devicePixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, devicePixels, devicePixels));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QHash<QString, QPixmap>& iconCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

QHash<QString, QString>& iconPathCache()
{
    static QHash<QString, QString> cache;
    return cache;
}

QString cacheKey(const QString& glyphName, IconColor color, int pixelSize, qreal dpr, Mode mode)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(glyphName)
        .arg(static_cast<int>(color))
        .arg(pixelSize)
        .arg(dpr)
        .arg(mode == Mode::Dark ? QStringLiteral("d") : QStringLiteral("l"));
}

// Both caches are invalidated together on a live theme flip — a mode change
// makes every IconColor resolve to a different QColor, so every cached
// pixmap/PNG is stale. Connected lazily (once) rather than at static-init
// time, since Notifier::instance() itself touches QGuiApplication.
void ensureInvalidationConnected()
{
    static const bool connected = [] {
        QObject::connect(&notifier(), &Notifier::changed, &notifier(), []() {
            iconCache().clear();
            iconPathCache().clear();
        });
        return true;
    }();
    Q_UNUSED(connected);
}

} // namespace

QIcon iconFromFile(const QString& svgPath, IconColor color, int pixelSize)
{
    ensureInvalidationConnected();
    const Mode mode = currentMode();
    const qreal dpr = devicePixelRatio();
    const QString key = cacheKey(svgPath, color, pixelSize, dpr, mode);

    QHash<QString, QPixmap>& cache = iconCache();
    auto it = cache.find(key);
    if (it == cache.end()) {
        const QColor resolved = colorFor(color, palette(mode));
        it = cache.insert(key, renderTinted(svgPath, resolved, pixelSize, dpr));
    }
    return QIcon(it.value());
}

QIcon iconFromFile(const QString& svgPath, const QColor& color, int pixelSize)
{
    const qreal dpr = devicePixelRatio();
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(svgPath, color.name(QColor::HexArgb)).arg(pixelSize).arg(dpr);
    QHash<QString, QPixmap>& cache = iconCache();
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, renderTinted(svgPath, color, pixelSize, dpr));
    return QIcon(it.value());
}

QIcon icon(const QString& name, IconColor color, int pixelSize)
{
    return iconFromFile(glyphPath(name), color, pixelSize);
}

QIcon iconWithColor(const QString& name, const QColor& color, int pixelSize)
{
    const qreal dpr = devicePixelRatio();
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(name, color.name(QColor::HexArgb)).arg(pixelSize).arg(dpr);

    QHash<QString, QPixmap>& cache = iconCache();
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, renderTinted(glyphPath(name), color, pixelSize, dpr));
    return QIcon(it.value());
}

QString iconAssetPath(const QString& glyphName, IconColor color, int pixelSize)
{
    ensureInvalidationConnected();
    const Mode mode = currentMode();
    // A fixed oversample factor, NOT the screen's actual devicePixelRatio:
    // this ends up saved as a plain PNG file consumed by a QSS `image:
    // url(...)` rule, which carries no devicePixelRatio metadata of its
    // own (unlike the QPixmap/QIcon path icon()/iconWithColor() use, where
    // setDevicePixelRatio() is honored by QPainter). Baking in the real
    // DPR here only matched Qt's on-screen rendering by coincidence at an
    // integer scale factor, and produced a mismatched, blurry glyph at a
    // fractional one (125%/150%). Oversampling by a fixed, generous amount
    // instead guarantees a sharp source to downscale from at whatever size
    // the QSS rule actually renders it at, integer scale or not.
    constexpr int kOversample = 4;
    const QString key = cacheKey(glyphName, color, pixelSize * kOversample, /*dpr=*/1.0, mode);

    QHash<QString, QString>& cache = iconPathCache();
    auto it = cache.find(key);
    if (it != cache.end())
        return it.value();

    const QColor resolved = colorFor(color, palette(mode));
    const QPixmap pixmap = renderTinted(glyphPath(glyphName), resolved, pixelSize * kOversample, /*dpr=*/1.0);

    const QString dir
        = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme-icons");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/%1.png").arg(qHash(key));
    pixmap.save(path, "PNG");

    it = cache.insert(key, path);
    return it.value();
}

} // namespace Theme
