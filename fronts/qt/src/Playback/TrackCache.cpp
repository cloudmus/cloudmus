#include "TrackCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcCache, "cloudmus.playback.cache")

const QLatin1String kPartSuffix(".part");
// A .part this old is left over from a run that ended mid-download. Younger
// ones may belong to another running instance.
constexpr int kStalePartSecs = 60 * 60;

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
    if (totalBytes_ <= maxBytes_)
        return;
    QList<QString> candidates;
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        if (!pinned_.contains(it.key()))
            candidates.append(it.key());
    }
    std::sort(candidates.begin(), candidates.end(),
        [this](const QString& a, const QString& b) { return entries_[a].lastUsed < entries_[b].lastUsed; });
    for (const QString& key : candidates) {
        if (totalBytes_ <= maxBytes_)
            break;
        qCDebug(lcCache) << "evicting" << key;
        removeFile(key);
    }
}

} // namespace Playback
