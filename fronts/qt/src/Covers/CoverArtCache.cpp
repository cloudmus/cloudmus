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
// Enough for a screenful of ordinary covers; a huge one (embedded art of a
// local file can be tens of megabytes decoded) is simply not kept.
constexpr int kSourceCacheBytes = 24 * 1024 * 1024;
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
    sourceCache_.setMaxCost(kSourceCacheBytes);
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
    if (QPixmap* cached = memoryCache_.object(cacheKey(url, targetSize)))
        return *cached;
    if (const QImage* source = sourceCache_.object(url))
        return store(url, targetSize, *source);
    const auto flight = inFlight_.find(url);
    if (flight != inFlight_.end()) {
        if (!flight->contains(targetSize))
            flight->append(targetSize);
        return { };
    }
    inFlight_.insert(url, { targetSize });
    fetch(url, QUrl(url), kMaxRedirects);
    return { };
}

QImage CoverArtCache::source(const QString& url) const
{
    const QImage* image = sourceCache_.object(url);
    return image != nullptr ? *image : QImage();
}

void CoverArtCache::request(const QString& url)
{
    if (url.isEmpty() || sourceCache_.contains(url) || inFlight_.contains(url))
        return;
    // No size asked for yet: only the decoded source is wanted.
    inFlight_.insert(url, { });
    fetch(url, QUrl(url), kMaxRedirects);
}

QPixmap CoverArtCache::store(const QString& url, QSize targetSize, const QImage& source)
{
    // Downscaled to exactly targetSize, with a non-square cover fitted
    // rather than stretched by whoever draws it into a square (see
    // setFitter()).
    const QPixmap fitted = fitter_
        ? fitter_(source, targetSize)
        : QPixmap::fromImage(source.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    memoryCache_.insert(cacheKey(url, targetSize), new QPixmap(fitted));
    return fitted;
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

void CoverArtCache::fetch(const QString& url, const QUrl& requestUrl, int redirectsLeft)
{
    QNetworkRequest request { requestUrl };
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, redirectsLeft]() {
        reply->deleteLater();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (redirect.isValid() && redirectsLeft > 0) {
            const QUrl next = reply->url().resolved(redirect);
            // The hop belongs to the same source, so it takes its route.
            const QString sourceId = sourceForUrl_.value(reply->url().toString());
            if (!sourceId.isEmpty())
                assignSource(sourceId, next.toString());
            fetch(url, next, redirectsLeft - 1);
            return;
        }
        const QList<QSize> sizes = inFlight_.take(url);
        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage full;
        if (!full.loadFromData(reply->readAll()))
            return;
        for (const QSize& size : sizes)
            store(url, size, full);
        // After the sizes above: an insert that doesn't fit deletes the image.
        sourceCache_.insert(url, new QImage(full), static_cast<int>(full.sizeInBytes()));
        emit pixmapReady(url);
    });
}

} // namespace Covers
