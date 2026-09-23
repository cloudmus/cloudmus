#include "CoverArtCache.h"

#include "GeneratedCoverArt.h"

#include <QImage>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

namespace Ui {

CoverArtCache::CoverArtCache(QObject* parent)
    : QObject(parent)
{
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
    fetch(url, targetSize);
    return { };
}

void CoverArtCache::fetch(const QString& url, QSize targetSize)
{
    const QString key = cacheKey(url, targetSize);
    if (inFlight_.contains(key))
        return;
    inFlight_.insert(key);

    QNetworkRequest request { QUrl(url) };
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, targetSize, key]() {
        reply->deleteLater();
        inFlight_.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage full;
        if (!full.loadFromData(reply->readAll()))
            return;
        // Decode-and-downscale immediately so the in-memory cache never
        // holds a full-resolution image — and to exactly targetSize, with
        // a non-square cover fitted over its own blur rather than
        // stretched by whoever draws it into a square (see fitCover()).
        memoryCache_.insert(key, new QPixmap(fitCover(full, targetSize)));
        emit pixmapReady(url);
    });
}

} // namespace Ui
