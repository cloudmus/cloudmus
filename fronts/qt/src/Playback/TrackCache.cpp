#include "TrackCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

#include "DiskSpace.h"

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcCache, "cloudmus.playback.cache")

const QLatin1String kPartSuffix(".part");
// A .part this old is left over from a run that ended mid-download. Younger
// ones may belong to another running instance.
constexpr int kStalePartSecs = 60 * 60;
// A disk filling up with no download of ours: another app's doing.
constexpr int kSpaceCheckMs = 15000;
// Downloading, how often to look: even a fast link leaves the margin far off.
constexpr int kWritingCheckMs = 1000;
// How much over a line it takes to be past it: a warning shouldn't come and go
// with every track, and what's freed shouldn't be taken again at once.
constexpr qint64 kSpaceMarginBytes = qint64(100) * 1024 * 1024;

} // namespace

TrackCache::TrackCache(const QString& directory, QObject* parent)
    : QObject(parent)
    , directory_(directory)
{
    QDir dir(directory_);
    if (!dir.mkpath(QStringLiteral(".")))
        qCWarning(lcCache) << "can't create" << directory_;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files)) {
        if (info.fileName().endsWith(kPartSuffix)) {
            if (info.lastModified().secsTo(now) > kStalePartSecs)
                QFile::remove(info.filePath());
            continue;
        }
        entries_.insert(info.fileName(), Entry { info.size(), info.lastModified() });
        totalBytes_ += info.size();
    }
    connect(&spaceTimer_, &QTimer::timeout, this, &TrackCache::checkSpace);
    spaceTimer_.start(kSpaceCheckMs);
    lastSpaceCheck_.start();
    checkSpace();
}

qint64 TrackCache::freeBytes() const
{
    return freeSpaceProvider_ ? freeSpaceProvider_() : DiskSpace::freeBytes(directory_);
}

void TrackCache::setFreeSpaceProvider(std::function<qint64()> provider)
{
    freeSpaceProvider_ = std::move(provider);
    checkSpace();
}

void TrackCache::writing()
{
    if (lastSpaceCheck_.elapsed() >= kWritingCheckMs)
        checkSpace();
}

bool TrackCache::wantsRoom() const
{
    const qint64 free = freeBytes();
    // Not knowing is no reason to delete anything.
    return free >= 0 && free < DiskSpace::kLowBytes + kSpaceMarginBytes;
}

void TrackCache::checkSpace()
{
    lastSpaceCheck_.restart();
    if (wantsRoom()) {
        const qint64 before = totalBytes_;
        trim();
        if (totalBytes_ != before)
            emit changed();
    }
    const qint64 free = freeBytes();
    Space now = Space::Fine;
    if (!DiskSpace::freeIsEnough(free, DiskSpace::kCriticalBytes))
        now = Space::Critical;
    else if (space_ == Space::Critical && !DiskSpace::freeIsEnough(free, DiskSpace::kCriticalBytes + kSpaceMarginBytes))
        now = Space::Critical;
    else if (!DiskSpace::freeIsEnough(free, DiskSpace::kLowBytes))
        now = Space::Low;
    else if (space_ != Space::Fine && !DiskSpace::freeIsEnough(free, DiskSpace::kLowBytes + kSpaceMarginBytes))
        now = Space::Low;
    if (now == space_)
        return;
    qCWarning(lcCache) << "free space:" << free << "bytes, now" << now;
    space_ = now;
    emit spaceChanged();
}

QString TrackCache::keyFor(const QString& sourceId, const QString& trackId)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(sourceId.toUtf8());
    hash.addData(QByteArrayView("\0", 1));
    hash.addData(trackId.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString TrackCache::filePath(const QString& key) const { return directory_ + QLatin1Char('/') + key; }

QString TrackCache::partPath(const QString& key) const { return filePath(key) + kPartSuffix; }

QString TrackCache::location(const QString& key) const { return entries_.contains(key) ? filePath(key) : QString(); }

QString TrackCache::path(const QString& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return { };
    const QString path = filePath(key);
    if (!QFileInfo::exists(path)) {
        // Deleted behind our back (someone cleaned the cache directory).
        totalBytes_ -= it->size;
        entries_.erase(it);
        emit changed();
        return { };
    }
    it->lastUsed = QDateTime::currentDateTime();
    QFile file(path);
    if (file.open(QIODevice::ReadWrite))
        file.setFileTime(it->lastUsed, QFileDevice::FileModificationTime);
    return path;
}

QString TrackCache::commit(const QString& key)
{
    const QString part = partPath(key);
    const QString path = filePath(key);
    const QFileInfo info(part);
    if (!info.exists())
        return { };
    const qint64 size = info.size(); // before the file moves from under it
    if (entries_.contains(key) && !removeFile(key))
        return { };
    if (!QFile::rename(part, path)) {
        qCWarning(lcCache) << "can't move" << part << "into the cache";
        return { };
    }
    entries_.insert(key, Entry { size, QDateTime::currentDateTime() });
    totalBytes_ += size;
    trim();
    emit changed();
    return path;
}

bool TrackCache::removeFile(const QString& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return true;
    const QString path = filePath(key);
    // A file still open (Windows: mpv playing it) can't go yet; it stays
    // counted and goes at a later trim.
    if (QFileInfo::exists(path) && !QFile::remove(path))
        return false;
    totalBytes_ -= it->size;
    entries_.erase(it);
    return true;
}

void TrackCache::remove(const QString& key)
{
    if (entries_.contains(key) && removeFile(key))
        emit changed();
}

void TrackCache::clear()
{
    bool removed = false;
    for (const QString& key : entries_.keys()) {
        if (!pinned_.contains(key))
            removed = removeFile(key) || removed;
    }
    if (removed)
        emit changed();
}

void TrackCache::setMaxBytes(qint64 bytes)
{
    if (maxBytes_ == bytes)
        return;
    maxBytes_ = bytes;
    const qint64 before = totalBytes_;
    trim();
    if (totalBytes_ != before)
        emit changed();
}

void TrackCache::setPinned(const QSet<QString>& keys) { pinned_ = keys; }

void TrackCache::trim()
{
    if (totalBytes_ <= maxBytes_ && !wantsRoom())
        return;
    QList<QString> candidates;
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        if (!pinned_.contains(it.key()))
            candidates.append(it.key());
    }
    std::sort(candidates.begin(), candidates.end(),
        [this](const QString& a, const QString& b) { return entries_[a].lastUsed < entries_[b].lastUsed; });
    for (const QString& key : candidates) {
        if (totalBytes_ <= maxBytes_ && !wantsRoom())
            break;
        qCDebug(lcCache) << "evicting" << key;
        removeFile(key);
    }
}

} // namespace Playback
