#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "FakeUpstream.h"
#include "ParallelDownload.h"

namespace Tests {

namespace {

QByteArray contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class ParallelDownloadTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        upstream_ = std::make_unique<FakeUpstream>();
        network_ = std::make_unique<QNetworkAccessManager>();
        network_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QVERIFY(directory_.isValid());
    }

    void cleanup()
    {
        network_.reset();
        upstream_.reset();
    }

    // The first range tells the length; then no more than `connections`
    // of the rest go at once.
    void fetchesRangesInParallel()
    {
        const QByteArray body = FakeUpstream::pattern(1000 * 1024);
        upstream_->bodies.insert(QStringLiteral("/f"), body);
        upstream_->held.insert(QStringLiteral("/f"));
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/f"))));
        download.setConnections(3);
        download.setRangeBytes(100 * 1024);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();

        QTRY_COMPARE(upstream_->requests.size(), 1);
        QTest::qWait(100);
        QCOMPARE(upstream_->requests.size(), 1);

        upstream_->release(QStringLiteral("/f"));
        upstream_->held.insert(QStringLiteral("/f"));
        QTRY_COMPARE(upstream_->requests.size(), 4);
        QTest::qWait(100);
        QCOMPARE(upstream_->requests.size(), 4);
        // Three connections at once: the server may see them in any order.
        QStringList parallel = upstream_->requests.mid(1);
        parallel.sort();
        QCOMPARE(parallel,
            (QStringList { QStringLiteral("/f@102400"), QStringLiteral("/f@204800"), QStringLiteral("/f@307200") }));
        QCOMPARE(download.totalLength(), qint64(body.size()));
        QCOMPARE(download.contiguousBytes(), qint64(100 * 1024));

        upstream_->release(QStringLiteral("/f"));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(upstream_->requests.size(), 10);
        QCOMPARE(download.receivedBytes(), qint64(body.size()));
        QCOMPARE(download.contiguousBytes(), qint64(body.size()));
        QCOMPARE(contents(path()), body);
    }

    // Bytes past a range still coming are progress, but not readable yet.
    void aGapHoldsTheContiguousBytesBack()
    {
        const QByteArray body = FakeUpstream::pattern(300 * 1024);
        upstream_->bodies.insert(QStringLiteral("/f"), body);
        upstream_->stallAfter.insert(QStringLiteral("/f"), 50 * 1024);
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/f"))));
        download.setConnections(3);
        download.setRangeBytes(100 * 1024);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();

        QTRY_COMPARE(download.receivedBytes(), qint64(250 * 1024));
        QCOMPARE(download.contiguousBytes(), qint64(50 * 1024));
        QVERIFY(completed.isEmpty());

        // The stalled range goes on from its byte.
        download.pause();
        download.start();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(upstream_->requests.last(), QStringLiteral("/f@51200"));
        QCOMPARE(contents(path()), body);
    }

    void aBrokenRangeIsPickedUpWhereItStopped()
    {
        const QByteArray body = FakeUpstream::pattern(300 * 1024);
        upstream_->bodies.insert(QStringLiteral("/f"), body);
        upstream_->cutAfter.insert(QStringLiteral("/f"), 30 * 1024);
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/f"))));
        download.setConnections(2);
        download.setRangeBytes(100 * 1024);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QVERIFY(upstream_->requests.contains(QStringLiteral("/f@30720")));
        QCOMPARE(contents(path()), body);
    }

    void aServerIgnoringRangeSendsItWhole()
    {
        const QByteArray body = FakeUpstream::pattern(300 * 1024);
        upstream_->bodies.insert(QStringLiteral("/f"), body);
        upstream_->wholeOnly.insert(QStringLiteral("/f"));
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/f"))));
        download.setConnections(4);
        download.setRangeBytes(100 * 1024);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(upstream_->requests, QStringList { QStringLiteral("/f@0") });
        QCOMPARE(download.totalLength(), qint64(body.size()));
        QCOMPARE(contents(path()), body);
    }

    // With no length to split by, one range after another until the end.
    void anUnknownLengthGoesRangeByRange()
    {
        const QByteArray body = FakeUpstream::pattern(250 * 1024);
        upstream_->bodies.insert(QStringLiteral("/f"), body);
        upstream_->lengthUnknown.insert(QStringLiteral("/f"));
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/f"))));
        download.setConnections(4);
        download.setRangeBytes(100 * 1024);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(upstream_->requests,
            (QStringList { QStringLiteral("/f@0"), QStringLiteral("/f@102400"), QStringLiteral("/f@204800"),
                QStringLiteral("/f@256000") }));
        QCOMPARE(download.totalLength(), qint64(body.size()));
        QCOMPARE(contents(path()), body);
    }

    // A URL that's no longer accepted: a fresh one takes over from what's
    // there.
    void anExpiredUrlIsReplaced()
    {
        const QByteArray body = FakeUpstream::pattern(300 * 1024);
        upstream_->bodies.insert(QStringLiteral("/old"), body);
        upstream_->bodies.insert(QStringLiteral("/new"), body);
        upstream_->cutAfter.insert(QStringLiteral("/old"), 40 * 1024);
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/old"))));
        download.setRangeBytes(100 * 1024);
        download.setConnections(2);
        QSignalSpy failed(&download, &Net::ParallelDownload::failed);
        QSignalSpy completed(&download, &Net::ParallelDownload::completed);
        download.start();
        QTRY_VERIFY(download.receivedBytes() >= 40 * 1024);
        upstream_->forbidden.insert(QStringLiteral("/old"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(failed.first().first().toBool(), true);
        QCOMPARE(download.failedStatus(), 403);
        QVERIFY(!download.isActive());

        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/new"))));
        download.start();
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!upstream_->requests.contains(QStringLiteral("/new@0")));
        QCOMPARE(contents(path()), body);
    }

    void aMissingFileFails()
    {
        Net::ParallelDownload download(path());
        download.setSource(network_.get(), QUrl(upstream_->url(QStringLiteral("/missing"))));
        QSignalSpy failed(&download, &Net::ParallelDownload::failed);
        download.start();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toBool(), false);
        QCOMPARE(download.failedStatus(), 404);
        QVERIFY(download.hasFailed());
    }

private:
    QString path() const { return directory_.filePath(QStringLiteral("file.part")); }

    QTemporaryDir directory_;
    std::unique_ptr<FakeUpstream> upstream_;
    std::unique_ptr<QNetworkAccessManager> network_;
};

QObject* makeParallelDownloadTest() { return new ParallelDownloadTest; }

} // namespace Tests

#include "ParallelDownloadTest.moc"
