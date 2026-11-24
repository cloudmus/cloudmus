#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>

namespace Playback {

// Downloaded tracks on disk, least recently used out first once they take
// more than the limit. A track is one file named after its key; while it
// downloads it is `<key>.part`, which the cache only hands a path for and
// takes over on commit(). Nothing here touches the network.
//
// There is no index file: the directory is the index, a file's
// modification time is when it was last played.
//
// It also watches the disk (see DiskSpace.h), whoever is filling it: tracks not
// pinned go first, whatever the limit, until there's room again.
class TrackCache : public QObject {
    Q_OBJECT

public:
    enum class Space {
        Fine,
        // Nothing is downloaded ahead; what's cached and not pinned goes.
        Low,
        // Nothing is downloaded at all, the playing track included.
        Critical,
    };
    Q_ENUM(Space)

    explicit TrackCache(const QString& directory, QObject* parent = nullptr);

    static QString keyFor(const QString& sourceId, const QString& trackId);

    bool contains(const QString& key) const { return entries_.contains(key); }
    // The complete file, counted as used now; empty if there is none.
    QString path(const QString& key);
    // Where the complete file is, without counting that as a use.
    QString location(const QString& key) const;
    QString partPath(const QString& key) const;
    // The finished download becomes the cached file. Returns its path;
    // empty if it couldn't be moved (the .part then stays where it is).
    QString commit(const QString& key);
    void remove(const QString& key);
    // Everything that isn't pinned.
    void clear();

    qint64 totalBytes() const { return totalBytes_; }
    qint64 maxBytes() const { return maxBytes_; }
    void setMaxBytes(qint64 bytes);
    // Never evicted, whatever the limit: what plays and what is about to.
    void setPinned(const QSet<QString>& keys);

    Space space() const { return space_; }
    qint64 freeBytes() const;
    // A look at the disk now, unless there was one a moment ago: for what's
    // writing to it. Bytes come fast enough for a timer to be too late.
    void writing();
    // For a test: where freeBytes() comes from.
    void setFreeSpaceProvider(std::function<qint64()> provider);

signals:
    // Something was added or removed.
    void changed();
    void spaceChanged();

private:
    struct Entry {
        qint64 size = 0;
        QDateTime lastUsed;
    };

    QString filePath(const QString& key) const;
    void trim();
    bool removeFile(const QString& key);
    void checkSpace();
    bool wantsRoom() const;

    QString directory_;
    QHash<QString, Entry> entries_;
    QSet<QString> pinned_;
    qint64 totalBytes_ = 0;
    qint64 maxBytes_ = qint64(1024) * 1024 * 1024;
    Space space_ = Space::Fine;
    std::function<qint64()> freeSpaceProvider_;
    QTimer spaceTimer_;
    QElapsedTimer lastSpaceCheck_;
};

} // namespace Playback
