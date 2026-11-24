#include "CoverArtCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>
#include <QNetworkDiskCache>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include "DiskSpace.h"
#include "ProxyRouting.h"

namespace Covers {

namespace {

Q_LOGGING_CATEGORY(lcCovers, "cloudmus.covers")
constexpr int kMaxRedirects = 5;
// Enough for a screenful of ordinary covers; a huge one (embedded art of a
// local file can be tens of megabytes decoded) is simply not kept.
constexpr int kSourceCacheBytes = 24 * 1024 * 1024;
// Each is a picture of a track or playlist, kept for the other process and for
// browsers to read; only the mobile front asks for any.
constexpr qint64 kFilesMaxBytes = qint64(200) * 1024 * 1024;
// What they're cut down to with the disk under its reserve: still a screenful
// and then some, for they're a cache like any other — they give way, they
// don't stop (without them the mobile front shows no covers at all).
constexpr qint64 kFilesLowBytes = qint64(16) * 1024 * 1024;
constexpr int kFilesTrimDelayMs = 30000;
// The largest side of a cover's file.
constexpr int kFileSide = 1024;
// What a local cover is decoded down to at the least: the other sizes asked
// for while it's read are made from it.
constexpr int kLocalReadSide = 1024;

// A local picture, decoded no larger than needed for `side` (its shorter side
// still that long, if it was); null if it can't be read.
QImage readScaled(const QString& path, int side)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (side > 0 && original.isValid() && (original.width() > side && original.height() > side))
        reader.setScaledSize(original.scaled(side, side, Qt::KeepAspectRatioByExpanding));
    QImage image = reader.read();
    if (image.isNull())
        qCWarning(lcCovers) << "can't read the cover" << path << reader.errorString();
    return image;
}
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
    filesDir_
        = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cloudmus/cover-files");
    memoryCache_.setMaxCost(64);
    sourceCache_.setMaxCost(kSourceCacheBytes);
    // Later: a long listing, at a start that has more pressing things to do.
    QTimer::singleShot(kFilesTrimDelayMs, this, [this]() { trimFiles(kFilesMaxBytes); });
}

void CoverArtCache::trimFiles(qint64 maxBytes)
{
    // They come back by themselves when wanted, and nothing else bounds them.
    QFileInfoList files = QDir(filesDir_).entryInfoList(QDir::Files, QDir::Time);
    qint64 total = 0;
    for (const QFileInfo& file : files) {
        total += file.size();
        if (total > maxBytes)
            QFile::remove(file.filePath());
    }
}

QString CoverArtCache::filePath(const QString& url) const
{
    const QString name = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    return filesDir_ + QLatin1Char('/') + name + QStringLiteral(".jpg");
}

QString CoverArtCache::localPath(const QString& url)
{
    if (!url.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return { };
    const QString path = QUrl(url).toLocalFile();
    return !path.isEmpty() && QFileInfo(path).isFile() ? path : QString();
}

QString CoverArtCache::localFile(const QString& url)
{
    if (url.isEmpty())
        return { };
    if (QString path = localPath(url); !path.isEmpty())
        return path;
    const QString file = filePath(url);
    if (QFile::exists(file))
        return file;
    const QImage* image = sourceCache_.object(url);
    if (image == nullptr) {
        filesWanted_.insert(url);
        request(url);
        return { };
    }
    return writeFile(url, *image);
}

QString CoverArtCache::writeFile(const QString& url, const QImage& image)
{
    const QString file = filePath(url);
    if (!Playback::DiskSpace::hasRoom(filesDir_))
        trimFiles(kFilesLowBytes);
    QDir().mkpath(filesDir_);
    // A local file's own picture can be thousands of pixels wide: more than
    // any screen shows a cover at, and slow for the other process to load.
    const QImage fitted = image.width() > kFileSide || image.height() > kFileSide
        ? image.scaled(kFileSide, kFileSide, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : image;
    // Written whole under another name: the other process may read it at once.
    const QString partial = file + QStringLiteral(".part");
    if (!fitted.save(partial, "JPG", 90) || !QFile::rename(partial, file)) {
        qCWarning(lcCovers) << "can't write the cover to" << file;
        QFile::remove(partial);
        return { };
    }
    return file;
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
    if (const QString path = localPath(url); !path.isEmpty()) {
        readLocal(url, path, targetSize);
        return { };
    }
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

void CoverArtCache::readLocal(const QString& url, const QString& path, QSize targetSize)
{
    if (unreadable_.contains(url))
        return;
    const auto flight = inFlight_.find(url);
    if (flight != inFlight_.end()) {
        if (!flight->contains(targetSize))
            flight->append(targetSize);
        return;
    }
    inFlight_.insert(url, { targetSize });
    const int side = qMax(kLocalReadSide, qMax(targetSize.width(), targetSize.height()));
    // Off the thread that paints: a large picture takes a while to decode.
    QThreadPool::globalInstance()->start([self = QPointer<CoverArtCache>(this), url, path, side] {
        const QImage image = readScaled(path, side);
        QMetaObject::invokeMethod(
            self.data(),
            [self, url, image] {
                if (!self)
                    return;
                const QList<QSize> sizes = self->inFlight_.take(url);
                if (image.isNull()) {
                    // Not again at every paint.
                    self->unreadable_.insert(url);
                    return;
                }
                for (const QSize& size : sizes)
                    self->store(url, size, image);
                emit self->pixmapReady(url);
            },
            Qt::QueuedConnection);
    });
}

QImage CoverArtCache::source(const QString& url) const
{
    if (const QString path = localPath(url); !path.isEmpty())
        return readScaled(path, 0);
    const QImage* image = sourceCache_.object(url);
    return image != nullptr ? *image : QImage();
}

void CoverArtCache::request(const QString& url)
{
    if (url.isEmpty() || sourceCache_.contains(url) || inFlight_.contains(url) || !localPath(url).isEmpty())
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
        if (reply->error() != QNetworkReply::NoError) {
            filesWanted_.remove(url);
            qCWarning(lcCovers) << "fetching" << url << "failed:" << reply->errorString();
            return;
        }

        QImage full;
        if (!full.loadFromData(reply->readAll())) {
            filesWanted_.remove(url);
            qCWarning(lcCovers) << "can't decode the cover" << url;
            return;
        }
        for (const QSize& size : sizes)
            store(url, size, full);
        if (filesWanted_.remove(url))
            writeFile(url, full);
        // After the sizes above: an insert that doesn't fit deletes the image.
        sourceCache_.insert(url, new QImage(full), static_cast<int>(full.sizeInBytes()));
        emit pixmapReady(url);
    });
}

} // namespace Covers
