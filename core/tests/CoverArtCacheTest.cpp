#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <limits>

#include "CoverArtCache.h"
#include "DiskSpace.h"

namespace Tests {

namespace {

QImage plain(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    return image;
}

// Where localFile() writes the covers it fetches.
QString filesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cloudmus/cover-files");
}

} // namespace

class CoverArtCacheTest : public QObject {
    Q_OBJECT

private slots:
    // A local cover is the file it's in: nothing is fetched, copied or kept.
    void aLocalCoverIsItsOwnFile()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath(QStringLiteral("cover.png"));
        QVERIFY(plain(800, 600).save(picture));
        const QString url = QUrl::fromLocalFile(picture).toString();
        QDir(filesDir()).removeRecursively();

        Covers::CoverArtCache covers;
        QSignalSpy ready(&covers, &Covers::CoverArtCache::pixmapReady);
        QCOMPARE(covers.localFile(url), picture);
        covers.request(url);
        QCOMPARE(covers.source(url).size(), QSize(800, 600));
        QVERIFY(!ready.wait(300));
        QVERIFY(!QDir(filesDir()).exists() || QDir(filesDir()).isEmpty());

        // Painted: read off the disk, on a worker. (The pixmap itself needs
        // a QGuiApplication, which these tests don't have.)
        QVERIFY(covers.pixmap(url, QSize(64, 64)).isNull());
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.last().constFirst().toString(), url);
    }

    // One from the internet too large to keep in memory (a service's own
    // full-size art) still gets its file once it's come, and smaller.
    void aHugeFetchedCoverStillGetsItsFile()
    {
        QBuffer png;
        png.open(QIODevice::WriteOnly);
        QVERIFY(plain(3200, 3000).save(&png, "PNG"));
        const QString url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.data().toBase64());

        Covers::CoverArtCache covers;
        QSignalSpy ready(&covers, &Covers::CoverArtCache::pixmapReady);
        QVERIFY(covers.localFile(url).isEmpty());
        QVERIFY(ready.wait(5000));
        const QString file = covers.localFile(url);
        QVERIFY(!file.isEmpty());
        const QImage written(file);
        QCOMPARE(written.width(), 1024);
        QCOMPARE(written.height(), 960);
        QFile::remove(file);
    }

    // Under the disk's reserve covers are a cache like any other: older ones
    // make way, a new one is still written — without files the mobile front
    // has no covers at all.
    void aShortDiskStillGetsCovers()
    {
        namespace Disk = Playback::DiskSpace;
        const auto restore = qScopeGuard([reserve = Disk::reserveBytes()]() { Disk::setReserveBytes(reserve); });
        QDir(filesDir()).removeRecursively();
        QVERIFY(QDir().mkpath(filesDir()));
        // Old covers, more than what's kept with the disk short.
        const QByteArray filler(1024 * 1024, 'x');
        for (int i = 0; i < 20; ++i) {
            QFile old(filesDir() + QStringLiteral("/old%1.jpg").arg(i));
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write(filler);
        }
        Disk::setReserveBytes(std::numeric_limits<qint64>::max() / 2);

        QBuffer png;
        png.open(QIODevice::WriteOnly);
        QVERIFY(plain(300, 300).save(&png, "PNG"));
        const QString url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.data().toBase64());
        Covers::CoverArtCache covers;
        QSignalSpy ready(&covers, &Covers::CoverArtCache::pixmapReady);
        QVERIFY(covers.localFile(url).isEmpty());
        QVERIFY(ready.wait(5000));
        const QString file = covers.localFile(url);
        QVERIFY(!file.isEmpty());
        QVERIFY(QFile::exists(file));
        QVERIFY(QDir(filesDir()).entryList(QDir::Files).size() < 20);
        QDir(filesDir()).removeRecursively();
    }
};

QObject* makeCoverArtCacheTest() { return new CoverArtCacheTest; }

} // namespace Tests

#include "CoverArtCacheTest.moc"
