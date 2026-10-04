#include "CoverArtCache.h"

#include <QImage>
#include <QNetworkDiskCache>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include "ProxyRouting.h"

namespace Covers {

namespace {
constexpr int kMaxRedirects = 5;
} // namespace

// Asks the cache, per request, which proxy that URL's source uses —
// QNetworkAccessManager otherwise has a single proxy for everything, and
// one manager has to stay: it owns the shared disk cache.
class RoutingProxyFactory : public QNetworkProxyFactory {
public:
    explicit RoutingProxyFactory(const CoverArtCache& cache)
        : cache_(cache)
    {
    }

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        return { cache_.proxyFor(query.url()) };
    }

private:
    const CoverArtCache& cache_;
};

CoverArtCache::CoverArtCache(QObject* parent)
    : QObject(parent)
{
    network_.setProxyFactory(new RoutingProxyFactory(*this)); // owned by network_
    auto* diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cloudmus/covers"));
    network_.setCache(diskCache);
    memoryCache_.setMaxCost(64);
}

QString CoverArtCache::cacheKey(const QString& url, QSize targetSize)
{
    return url + QStringLiteral("@") + QString::number(targetSize.width()) + QStringLiteral("x")
        + QString::number(targetSize.height());
}

QPixmap CoverArtCache::pixmap(const QString& url, QSize targetSize)
{
    if (url.isEmpty())
        return { };
    const QString key = cacheKey(url, targetSize);
    if (QPixmap* cached = memoryCache_.object(key)) {
        return *cached;
    }
    if (!inFlight_.contains(key)) {
        inFlight_.insert(key);
        fetch(url, targetSize, QUrl(url), kMaxRedirects);
    }
    return { };
}

void CoverArtCache::assignSource(const QString& sourceId, const QString& url)
{
    if (!url.isEmpty())
        sourceForUrl_.insert(url, sourceId);
}

void CoverArtCache::assignSource(const QString& sourceId, const QList<Track>& tracks)
{
    for (const Track& track : tracks) {
        assignSource(sourceId, track.coverUrl.value_or(QString()));
        if (track.album)
            assignSource(sourceId, track.album->coverUrl.value_or(QString()));
    }
}

QNetworkProxy CoverArtCache::proxyFor(const QUrl& url) const
{
    const QString sourceId = sourceForUrl_.value(url.toString());
    if (!sourceId.isEmpty() && proxyProvider_)
        return proxyProvider_(sourceId, url);
    return Net::systemProxy(url);
}

void CoverArtCache::fetch(const QString& url, QSize targetSize, const QUrl& requestUrl, int redirectsLeft)
{
    const QString key = cacheKey(url, targetSize);
    QNetworkRequest request { requestUrl };
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, targetSize, key, redirectsLeft]() {
        reply->deleteLater();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (redirect.isValid() && redirectsLeft > 0) {
            const QUrl next = reply->url().resolved(redirect);
            // The hop belongs to the same source, so it takes its route.
            const QString sourceId = sourceForUrl_.value(reply->url().toString());
            if (!sourceId.isEmpty())
                assignSource(sourceId, next.toString());
            fetch(url, targetSize, next, redirectsLeft - 1);
            return;
        }
        inFlight_.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage full;
        if (!full.loadFromData(reply->readAll()))
            return;
        // Decode-and-downscale immediately so the in-memory cache never
        // holds a full-resolution image — and to exactly targetSize, with
        // a non-square cover fitted rather than stretched by whoever draws
        // it into a square (see setFitter()).
        const QPixmap fitted = fitter_
            ? fitter_(full, targetSize)
            : QPixmap::fromImage(full.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        memoryCache_.insert(key, new QPixmap(fitted));
        emit pixmapReady(url);
    });
}

} // namespace Covers
