#pragma once

#include <QCache>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>

namespace Ui {

// Async cover-art fetch + decode-and-downscale-immediately + small
// in-memory LRU, backed by a QNetworkDiskCache so repeat plays don't
// re-download art. Callers (the track-row delegate, the now-playing bar)
// call pixmap() for an immediate (possibly null) result and connect to
// pixmapReady() to repaint once a background fetch completes — this is what
// makes track-list thumbnails "fetch lazily for visible rows only": a
// delegate only calls pixmap() for rows Qt actually asks it to paint.
class CoverArtCache : public QObject {
    Q_OBJECT

public:
    explicit CoverArtCache(QObject* parent = nullptr);

    QPixmap pixmap(const QString& url, QSize targetSize);

signals:
    void pixmapReady(QString url);

private:
    void fetch(const QString& url, QSize targetSize);
    static QString cacheKey(const QString& url, QSize targetSize);

    QNetworkAccessManager network_;
    QCache<QString, QPixmap> memoryCache_;
    QSet<QString> inFlight_;
};

} // namespace Ui
