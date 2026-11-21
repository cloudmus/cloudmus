#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "DiskSpace.h"
#include "TrackCache.h"

namespace Tests {

class TrackCacheTest : public QObject {
    Q_OBJECT

private:
    // A file of `size` bytes last modified `ageSecs` ago.
    static bool makeFile(const QString& path, qint64 size, int ageSecs = 0)
    {
        QFile file(path);
        // Flushed first: a write still buffered would stamp the file anew.
        if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(size, 'x')) != size || !file.flush())
            return false;
        return file.setFileTime(QDateTime::currentDateTime().addSecs(-ageSecs), QFileDevice::FileModificationTime);
    }

private slots:
    void aFinishedDownloadBecomesTheCachedFile()
    {
        QTemporaryDir dir;
        Playback::TrackCache cache(dir.path());
        QSignalSpy changed(&cache, &Playback::TrackCache::changed);
        const QString key = Playback::TrackCache::keyFor(QStringLiteral("source"), QStringLiteral("track"));
        QVERIFY(key != Playback::TrackCache::keyFor(QStringLiteral("sourcet"), QStringLiteral("rack")));
        QVERIFY(!cache.contains(key));
        QVERIFY(cache.path(key).isEmpty());

        QVERIFY(makeFile(cache.partPath(key), 1000));
        const QString path = cache.commit(key);
        QVERIFY(!path.isEmpty());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(cache.partPath(key)));
        QVERIFY(cache.contains(key));
        QCOMPARE(cache.path(key), path);
        QCOMPARE(cache.totalBytes(), qint64(1000));
        QCOMPARE(changed.size(), 1);

        cache.remove(key);
        QVERIFY(!cache.contains(key));
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(cache.totalBytes(), qint64(0));
    }

    // Whatever the limit: a disk filling up costs the tracks not in use, and
    // says so, on the way down and back.
    void aFillingDiskTakesTheCacheAndSaysSo()
    {
        using Space = Playback::TrackCache::Space;
        namespace Disk = Playback::DiskSpace;
        QTemporaryDir dir;
        QVERIFY(makeFile(dir.filePath(QStringLiteral("oldest")), 1000, 900));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("old")), 1000, 300));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("pinned")), 1000, 1200));
        Playback::TrackCache cache(dir.path());
        cache.setPinned({ QStringLiteral("pinned") });
        qint64 free = Disk::kLowBytes * 2;
        Playback::TrackCache* cacheAt = &cache;
        // Each file gone gives its share back, a lot of it for the test's sake.
        qint64 freedPerFile = 0;
        const qint64 total = cache.totalBytes();
        cache.setFreeSpaceProvider([&]() { return free + (total - cacheAt->totalBytes()) / 1000 * freedPerFile; });
        QCOMPARE(cache.space(), Space::Fine);
        QCOMPARE(cache.totalBytes(), total);
        QSignalSpy space(&cache, &Playback::TrackCache::spaceChanged);

        // Short by less than a file: the oldest is enough, the warning stays off.
        freedPerFile = 1024 * 1024 * 1024;
        free = Disk::kLowBytes - 1024 * 1024;
        cache.setFreeSpaceProvider([&]() { return free + (total - cacheAt->totalBytes()) / 1000 * freedPerFile; });
        QCOMPARE(cache.space(), Space::Fine);
        QVERIFY(!cache.contains(QStringLiteral("oldest")));
        QVERIFY(cache.contains(QStringLiteral("old")));
        QCOMPARE(space.size(), 0);

        // Nothing left to free but what's pinned: never that, and it's told.
        freedPerFile = 0;
        free = Disk::kLowBytes - 1;
        cache.setFreeSpaceProvider([&]() { return free; });
        QVERIFY(!cache.contains(QStringLiteral("old")));
        QVERIFY(cache.contains(QStringLiteral("pinned")));
        QCOMPARE(cache.space(), Space::Low);
        QCOMPARE(space.size(), 1);
        free = Disk::kCriticalBytes - 1;
        cache.setFreeSpaceProvider([&]() { return free; });
        QCOMPARE(cache.space(), Space::Critical);
        QCOMPARE(space.size(), 2);

        // Back only with a margin: a line crossed to and fro warns once.
        free = Disk::kCriticalBytes + 1;
        cache.setFreeSpaceProvider([&]() { return free; });
        QCOMPARE(cache.space(), Space::Critical);
        free = Disk::kLowBytes + 1;
        cache.setFreeSpaceProvider([&]() { return free; });
        QCOMPARE(cache.space(), Space::Low);
        free = Disk::kLowBytes * 2;
        cache.setFreeSpaceProvider([&]() { return free; });
        QCOMPARE(cache.space(), Space::Fine);
    }

    // Not knowing frees nothing.
    void anUnknownDiskRemovesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(makeFile(dir.filePath(QStringLiteral("a")), 1000));
        Playback::TrackCache cache(dir.path());
        cache.setFreeSpaceProvider([]() { return qint64(-1); });
        QVERIFY(cache.contains(QStringLiteral("a")));
    }

    void whatWasPlayedLongestAgoGoesFirst()
    {
        QTemporaryDir dir;
        QVERIFY(makeFile(dir.filePath(QStringLiteral("old")), 400, 300));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("older")), 400, 600));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("oldest")), 400, 900));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("recent")), 400, 10));

        // What's on disk from an earlier run is the cache.
        Playback::TrackCache cache(dir.path());
        QCOMPARE(cache.totalBytes(), qint64(1600));
        // Played now, so no longer the oldest.
        QVERIFY(!cache.path(QStringLiteral("oldest")).isEmpty());
        // Wanted for what plays next: kept whatever its age.
        cache.setPinned({ QStringLiteral("older") });

        cache.setMaxBytes(1000);
        QVERIFY(!cache.contains(QStringLiteral("old")));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("old"))));
        QVERIFY(cache.contains(QStringLiteral("older")));
        QVERIFY(cache.contains(QStringLiteral("oldest")));
        QVERIFY(!cache.contains(QStringLiteral("recent")));
        QCOMPARE(cache.totalBytes(), qint64(800));

        // A new track pushes out what there's no room for.
        cache.setPinned({ });
        QVERIFY(makeFile(cache.partPath(QStringLiteral("new")), 400));
        QVERIFY(!cache.commit(QStringLiteral("new")).isEmpty());
        QVERIFY(!cache.contains(QStringLiteral("older")));
        QVERIFY(cache.contains(QStringLiteral("new")));
        QCOMPARE(cache.totalBytes(), qint64(800));
    }

    void abandonedPartsAreSweptAtStartup()
    {
        QTemporaryDir dir;
        QVERIFY(makeFile(dir.filePath(QStringLiteral("a.part")), 100, 3 * 60 * 60));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("b.part")), 100, 60)); // maybe another instance's
        Playback::TrackCache cache(dir.path());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("a.part"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("b.part"))));
        QCOMPARE(cache.totalBytes(), qint64(0));
    }

    void clearingLeavesWhatIsWanted()
    {
        QTemporaryDir dir;
        QVERIFY(makeFile(dir.filePath(QStringLiteral("playing")), 100));
        QVERIFY(makeFile(dir.filePath(QStringLiteral("other")), 100));
        Playback::TrackCache cache(dir.path());
        cache.setPinned({ QStringLiteral("playing") });
        cache.clear();
        QVERIFY(cache.contains(QStringLiteral("playing")));
        QVERIFY(!cache.contains(QStringLiteral("other")));
        QCOMPARE(cache.totalBytes(), qint64(100));
    }
};

QObject* makeTrackCacheTest() { return new TrackCacheTest; }

} // namespace Tests

#include "TrackCacheTest.moc"
