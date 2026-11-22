#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "CoverArtCache.h"

namespace Tests {

class CoverArtCacheTest : public QObject {
    Q_OBJECT

private:
    // A cover on disk, read from its file each time it's wanted.
    static QString writeCover(const QTemporaryDir& dir, int side = 200)
    {
        QImage image(side, side, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        const QString path = dir.filePath(QStringLiteral("cover.png"));
        return image.save(path) ? QUrl::fromLocalFile(path).toString() : QString();
    }

    // A cover that's fetched, as from the internet, with no server.
    static QString fetchedCover()
    {
        QImage image(200, 200, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QBuffer png;
        png.open(QIODevice::WriteOnly);
        image.save(&png, "PNG");
        return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.data().toBase64());
    }

private slots:
    void aCoverIsFetchedOnceForEverySizeAskedMeanwhile()
    {
        QTemporaryDir dir;
        const QString url = writeCover(dir);
        QVERIFY(!url.isEmpty());
        Covers::CoverArtCache cache;
        QSignalSpy ready(&cache, &Covers::CoverArtCache::pixmapReady);

        QVERIFY(cache.pixmap(url, QSize(40, 40)).isNull());
        QVERIFY(cache.pixmap(url, QSize(120, 120)).isNull());
        QVERIFY(ready.wait(5000));
        QTest::qWait(50);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(cache.pixmap(url, QSize(40, 40)).size(), QSize(40, 40));
        QCOMPARE(cache.pixmap(url, QSize(120, 120)).size(), QSize(120, 120));
    }

    // One from the internet is kept: another size of it needs no fetch.
    void aFetchedCoverIsThereAtOnceInAnotherSize()
    {
        const QString url = fetchedCover();
        Covers::CoverArtCache cache;
        QSignalSpy ready(&cache, &Covers::CoverArtCache::pixmapReady);
        cache.pixmap(url, QSize(40, 40));
        QVERIFY(ready.wait(5000));

        QCOMPARE(cache.pixmap(url, QSize(160, 160)).size(), QSize(160, 160));
        QCOMPARE(ready.count(), 1);
    }

    // A large local one comes, read from its file, at the size asked.
    void aLocalCoverIsReadAtTheSizeAsked()
    {
        QTemporaryDir dir;
        const QString url = writeCover(dir, 3000);
        QVERIFY(!url.isEmpty());
        Covers::CoverArtCache cache;
        QSignalSpy ready(&cache, &Covers::CoverArtCache::pixmapReady);
        QVERIFY(cache.pixmap(url, QSize(160, 160)).isNull());
        QVERIFY(ready.wait(5000));
        QCOMPARE(cache.pixmap(url, QSize(160, 160)).size(), QSize(160, 160));
        // Another size is read again from the file: nothing else is kept.
        QVERIFY(cache.pixmap(url, QSize(64, 64)).isNull());
        QVERIFY(ready.wait(5000));
        QCOMPARE(cache.pixmap(url, QSize(64, 64)).size(), QSize(64, 64));
    }
};

QObject* makeCoverArtCacheTest() { return new CoverArtCacheTest; }

} // namespace Tests

#include "CoverArtCacheTest.moc"
