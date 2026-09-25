#pragma once

#include <QCache>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QUrl>

#include <functional>

#include "Models.h"

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

    // Covers are fetched the way their source connects (its proxy, see
    // Net::ProxyRouting): the cache learns which source a cover URL came
    // from as the source's tracks and playlists arrive, and asks
    // `provider` for that source's proxy. URLs it hasn't been told about
    // go as the system has it.
    using ProxyProvider = std::function<QNetworkProxy(const QString& sourceId, const QUrl& url)>;
    void setProxyProvider(ProxyProvider provider) { proxyProvider_ = std::move(provider); }
    void assignSource(const QString& sourceId, const QString& url);
    // Every cover a track carries (its own, its album's).
    void assignSource(const QString& sourceId, const QList<Track>& tracks);

signals:
    void pixmapReady(QString url);

private:
    friend class RoutingProxyFactory;

    // `requestUrl`: where to fetch from — `url` itself, or where it
    // redirected to (followed by hand, so the hop keeps the source's proxy).
    void fetch(const QString& url, QSize targetSize, const QUrl& requestUrl, int redirectsLeft);
    QNetworkProxy proxyFor(const QUrl& url) const;
    static QString cacheKey(const QString& url, QSize targetSize);

    QNetworkAccessManager network_;
    QCache<QString, QPixmap> memoryCache_;
    QSet<QString> inFlight_;
    QHash<QString, QString> sourceForUrl_;
    ProxyProvider proxyProvider_;
};

} // namespace Ui
