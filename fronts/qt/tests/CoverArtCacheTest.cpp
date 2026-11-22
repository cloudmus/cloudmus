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

private slots:
    // A local file's own picture can be too large to keep in memory: its
    // file is written all the same, once it's come, and smaller.
    void aHugeCoverStillGetsItsFile()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath(QStringLiteral("cover.png"));
        QImage huge(3200, 3000, QImage::Format_RGB32);
        huge.fill(Qt::darkCyan);
        QVERIFY(huge.save(picture));
        const QString url = QUrl::fromLocalFile(picture).toString();

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
};

QObject* makeCoverArtCacheTest() { return new CoverArtCacheTest; }

} // namespace Tests

#include "CoverArtCacheTest.moc"
