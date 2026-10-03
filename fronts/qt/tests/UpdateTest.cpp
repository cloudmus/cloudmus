#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "ReleaseFeed.h"
#include "Settings.h"
#include "UpdateChecker.h"
#include "UpdateDownloader.h"
#ifndef Q_OS_WIN
#include "Installer.h"
#endif

namespace Tests {

namespace {

QJsonObject release(const QString& tag, const QString& body, bool withAsset = true)
{
    QJsonArray assets;
    if (withAsset) {
        const QString name = QStringLiteral("CloudMus-%1%2").arg(tag, Update::platformAssetSuffix());
        assets.append(QJsonObject {
            { QStringLiteral("name"), name },
            { QStringLiteral("browser_download_url"), QStringLiteral("https://example.org/") + name },
            { QStringLiteral("size"), 1234 },
        });
    }
    return QJsonObject {
        { QStringLiteral("tag_name"), tag },
        { QStringLiteral("body"), body },
        { QStringLiteral("html_url"), QStringLiteral("https://example.org/releases/") + tag },
        { QStringLiteral("published_at"), QStringLiteral("2026-09-1%1T12:00:00Z").arg(tag.right(1)) },
        { QStringLiteral("draft"), false },
        { QStringLiteral("prerelease"), false },
        { QStringLiteral("assets"), assets },
    };
}

QByteArray feed(const QJsonArray& releases) { return QJsonDocument(releases).toJson(); }

// A one-route HTTP server: answers every request with `body`, sent in
// `chunks` pieces `chunkDelayMs` apart.
class HttpStub : public QObject {
public:
    QByteArray body;
    int status = 200;
    int chunks = 1;
    int chunkDelayMs = 0;
    // Announced in Content-Length; the body's own size if negative.
    qint64 contentLength = -1;

    HttpStub()
    {
        server_.listen(QHostAddress::LocalHost);
        connect(&server_, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = server_.nextPendingConnection())
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    if (!socket->readAll().contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    answer(socket);
                });
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/file").arg(server_.serverPort())); }

private:
    void answer(QTcpSocket* socket)
    {
        socket->write(QStringLiteral("HTTP/1.1 %1 X\r\nContent-Length: %2\r\nConnection: close\r\n\r\n")
                .arg(status)
                .arg(contentLength >= 0 ? contentLength : body.size())
                .toLatin1());
        const qsizetype chunkSize = (body.size() + chunks - 1) / qMax(chunks, 1);
        for (int i = 0; i < chunks; ++i) {
            const QByteArray chunk = body.mid(i * chunkSize, chunkSize);
            QTimer::singleShot(i * chunkDelayMs, socket, [socket, chunk, last = i == chunks - 1]() {
                socket->write(chunk);
                if (last)
                    socket->disconnectFromHost();
            });
        }
    }

    QTcpServer server_;
};

class UpdateTest : public QObject {
    Q_OBJECT

private slots:
    void versionsCompare()
    {
        const auto v = [](const char* text) { return *Update::parseVersion(QString::fromLatin1(text)); };
        QVERIFY(v("0.10.0") > v("0.9.9"));
        QVERIFY(v("v1.2.3") == v("1.2.3"));
        // A dev build sorts after its tag, before the next release.
        QVERIFY(v("0.5.0-3-gabc1234") > v("0.5.0"));
        QVERIFY(v("0.5.0-3-gabc1234-dirty") < v("0.5.1"));
        QVERIFY(v("0.1.0+gabc1234").dev);
        QVERIFY(!v("0.5.0").dev);
        QVERIFY(!Update::parseVersion(QStringLiteral("abc")));
    }

    void parsesReleasesNewestFirst()
    {
        QJsonObject draft = release(QStringLiteral("0.9.0"), QStringLiteral("draft"));
        draft[QStringLiteral("draft")] = true;
        QJsonObject pre = release(QStringLiteral("0.8.0"), QStringLiteral("pre"));
        pre[QStringLiteral("prerelease")] = true;
        const QJsonArray releases { release(QStringLiteral("0.5.0"), QStringLiteral("- five")), draft, pre,
            release(QStringLiteral("0.7.0"), QStringLiteral("- seven")),
            release(QStringLiteral("0.6.0"), QStringLiteral("no asset"), false),
            release(QStringLiteral("0.6.1-rc1"), QStringLiteral("rc")) };
        const auto parsed = Update::parseReleases(feed(releases), Update::platformAssetSuffix());
        QVERIFY(parsed);
        QCOMPARE(parsed->size(), 2);
        QCOMPARE(parsed->at(0).name, QStringLiteral("0.7.0"));
        QCOMPARE(parsed->at(0).notes, QStringLiteral("- seven"));
        QCOMPARE(parsed->at(0).assetSize, 1234);
        QCOMPARE(parsed->at(1).name, QStringLiteral("0.5.0"));
        QVERIFY(!Update::parseReleases("{}", Update::platformAssetSuffix()));
    }

    void pendingUpdateGathersEveryNewerRelease()
    {
        const QJsonArray releases { release(QStringLiteral("0.7.0"), QStringLiteral("- seven")),
            release(QStringLiteral("0.6.0"), QStringLiteral("- six")),
            release(QStringLiteral("0.5.0"), QStringLiteral("- five")) };
        const auto parsed = *Update::parseReleases(feed(releases), Update::platformAssetSuffix());
        const auto update = Update::pendingUpdate(parsed, *Update::parseVersion(QStringLiteral("0.5.0")));
        QVERIFY(update);
        QCOMPARE(update->releases.size(), 2);
        QCOMPARE(update->latest().name, QStringLiteral("0.7.0"));
        QCOMPARE(update->releases[1].name, QStringLiteral("0.6.0"));
        QCOMPARE(update->releases[1].published, QDate(2026, 9, 10));
        QCOMPARE(update->currentPublished, QDate(2026, 9, 10));
        QVERIFY(!Update::pendingUpdate(parsed, *Update::parseVersion(QStringLiteral("0.7.0"))));
        // A dev build knows the date of the release it is built on.
        const auto own = Update::releaseOf(parsed, *Update::parseVersion(QStringLiteral("0.6.0-4-gabc")));
        QVERIFY(own);
        QCOMPARE(own->name, QStringLiteral("0.6.0"));
        QVERIFY(!Update::releaseOf(parsed, *Update::parseVersion(QStringLiteral("0.4.0"))));
        // A build past the last tag is up to date with it.
        QVERIFY(!Update::pendingUpdate(parsed, *Update::parseVersion(QStringLiteral("0.7.0-2-gabc"))));
    }

    void skippedVersionIsOfferedOnlyWhenAsked()
    {
        Config::Settings settings;
        settings.setSkippedUpdateVersion(QString());
        HttpStub server;
        server.body = feed({ release(QStringLiteral("0.7.0"), QStringLiteral("- seven")) });
        Update::UpdateChecker checker(settings);
        checker.configure(QStringLiteral("0.6.0"), server.url());
        QSignalSpy available(&checker, &Update::UpdateChecker::updateAvailable);

        checker.check(false);
        QTRY_COMPARE(available.size(), 1);
        checker.skip(QStringLiteral("0.7.0"));

        QSignalSpy failed(&checker, &Update::UpdateChecker::checkFailed);
        checker.check(false);
        QTest::qWait(300);
        QCOMPARE(available.size(), 1);
        QCOMPARE(failed.size(), 0);

        checker.check(true);
        QTRY_COMPARE(available.size(), 2);
        QCOMPARE(available.last().at(1).toBool(), true);

        // A newer one than the skipped is offered again.
        server.body
            = feed({ release(QStringLiteral("0.7.1"), QString()), release(QStringLiteral("0.7.0"), QString()) });
        checker.check(false);
        QTRY_COMPARE(available.size(), 3);
        settings.setSkippedUpdateVersion(QString());
    }

    void upToDateOnlyReportedWhenManual()
    {
        Config::Settings settings;
        HttpStub server;
        server.body = feed({ release(QStringLiteral("0.6.0"), QString()) });
        Update::UpdateChecker checker(settings);
        checker.configure(QStringLiteral("0.6.0"), server.url());
        QSignalSpy upToDate(&checker, &Update::UpdateChecker::upToDate);
        checker.check(false);
        QTest::qWait(300);
        QCOMPARE(upToDate.size(), 0);
        checker.check(true);
        QTRY_COMPARE(upToDate.size(), 1);

        server.status = 500;
        QSignalSpy failed(&checker, &Update::UpdateChecker::checkFailed);
        checker.check(true);
        QTRY_COMPARE(failed.size(), 1);
    }

    void downloadsWithProgress()
    {
        QTemporaryDir dir;
        HttpStub server;
        server.body = QByteArray(200 * 1024, 'x');
        server.chunks = 8;
        server.chunkDelayMs = 100;
        QNetworkAccessManager network;
        Update::UpdateDownloader downloader(network);
        QSignalSpy progress(&downloader, &Update::UpdateDownloader::progress);
        QSignalSpy finished(&downloader, &Update::UpdateDownloader::finished);
        const QString path = dir.filePath(QStringLiteral("update.part"));
        downloader.start(server.url(), server.body.size(), path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(finished.first().first().toString(), path);
        QVERIFY(progress.size() > 1);
        QCOMPARE(progress.last().at(0).toLongLong(), server.body.size());
        bool estimated = false;
        for (const QList<QVariant>& args : progress)
            estimated = estimated || args.at(2).toInt() >= 0;
        QVERIFY(estimated);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), server.body);
    }

    void cancelledDownloadLeavesNoFile()
    {
        QTemporaryDir dir;
        HttpStub server;
        server.body = QByteArray(100 * 1024, 'x');
        server.chunks = 10;
        server.chunkDelayMs = 200;
        QNetworkAccessManager network;
        Update::UpdateDownloader downloader(network);
        QSignalSpy progress(&downloader, &Update::UpdateDownloader::progress);
        QSignalSpy finished(&downloader, &Update::UpdateDownloader::finished);
        const QString path = dir.filePath(QStringLiteral("update.part"));
        downloader.start(server.url(), server.body.size(), path);
        QTRY_VERIFY(!progress.isEmpty());
        QVERIFY(QFile::exists(path));
        downloader.cancel();
        QVERIFY(!downloader.isRunning());
        QVERIFY(!QFile::exists(path));
        QTest::qWait(300);
        QCOMPARE(finished.size(), 0);
    }

    void truncatedDownloadFails()
    {
        QTemporaryDir dir;
        HttpStub server;
        server.body = QByteArray(1000, 'x');
        QNetworkAccessManager network;
        Update::UpdateDownloader downloader(network);
        QSignalSpy failed(&downloader, &Update::UpdateDownloader::failed);
        const QString path = dir.filePath(QStringLiteral("update.part"));
        downloader.start(server.url(), 2000, path);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!QFile::exists(path));
    }

#ifndef Q_OS_WIN
    void appImageTakesItsNewName()
    {
        using Update::Installer::appImageTarget;
        QCOMPARE(appImageTarget(QStringLiteral("/apps/CloudMus-0.5.0-x86_64.AppImage"),
                     QStringLiteral("CloudMus-0.6.0-x86_64.AppImage")),
            QStringLiteral("/apps/CloudMus-0.6.0-x86_64.AppImage"));
        // Renamed by the user: kept.
        QCOMPARE(
            appImageTarget(QStringLiteral("/apps/cloudmus.AppImage"), QStringLiteral("CloudMus-0.6.0-x86_64.AppImage")),
            QStringLiteral("/apps/cloudmus.AppImage"));
    }

    void appImageIsReplaced()
    {
        QTemporaryDir dir;
        const auto write = [](const QString& path, const QByteArray& data) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(data);
        };
        const QString old = dir.filePath(QStringLiteral("CloudMus-0.5.0-x86_64.AppImage"));
        const QString part = dir.filePath(QStringLiteral(".CloudMus-0.6.0-x86_64.AppImage.part"));
        const QString target = dir.filePath(QStringLiteral("CloudMus-0.6.0-x86_64.AppImage"));
        write(old, "old");
        write(part, "new");
        QString error;
        QVERIFY2(Update::Installer::replaceAppImage(part, old, target, &error), qPrintable(error));
        QVERIFY(!QFile::exists(old));
        QVERIFY(!QFile::exists(part));
        QVERIFY(QFileInfo(target).isExecutable());

        // Same name: replaced in place.
        write(part, "newer");
        QVERIFY2(Update::Installer::replaceAppImage(part, target, target, &error), qPrintable(error));
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("newer"));
    }
#endif
};

} // namespace

QObject* makeUpdateTest() { return new UpdateTest; }

} // namespace Tests

#include "UpdateTest.moc"
