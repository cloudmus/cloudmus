#include <QFile>
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
    // A cover on disk: the cache fetches file:// the way it fetches http(s).
    static QString writeCover(const QTemporaryDir& dir)
    {
        QImage image(200, 200, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        const QString path = dir.filePath(QStringLiteral("cover.png"));
        return image.save(path) ? QUrl::fromLocalFile(path).toString() : QString();
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

    void aLoadedCoverIsThereAtOnceInAnotherSize()
    {
        QTemporaryDir dir;
        const QString url = writeCover(dir);
        QVERIFY(!url.isEmpty());
        Covers::CoverArtCache cache;
        QSignalSpy ready(&cache, &Covers::CoverArtCache::pixmapReady);
        cache.pixmap(url, QSize(40, 40));
        QVERIFY(ready.wait(5000));

        // The file is gone: it can only come from what is already loaded.
        QVERIFY(QFile::remove(QUrl(url).toLocalFile()));
        QCOMPARE(cache.pixmap(url, QSize(160, 160)).size(), QSize(160, 160));
        QCOMPARE(ready.count(), 1);
    }
};

QObject* makeCoverArtCacheTest() { return new CoverArtCacheTest; }

} // namespace Tests

#include "CoverArtCacheTest.moc"
