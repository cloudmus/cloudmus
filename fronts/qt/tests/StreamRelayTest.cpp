#include <QDataStream>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "AudioPlayer.h"
#include "FakeUpstream.h"
#include "NetworkPool.h"
#include "ProxyRouting.h"
#include "StreamDownload.h"
#include "StreamRelay.h"

namespace Tests {

class StreamRelayTest : public QObject {
    Q_OBJECT

private slots:
    // As main() does: mpv must reach the local relay even with an
    // http_proxy in the environment.
    void initTestCase() { Net::bypassProxyForLoopback(); }

    // The System connection must stream through the proxy the backend got
    // from the environment: a URL resolved through it may only work there.
    void systemConnectionTakesTheEnvironmentsProxy()
    {
        const char* const names[] = { "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY", "no_proxy", "NO_PROXY" };
        QList<std::pair<const char*, std::optional<QByteArray>>> saved;
        for (const char* name : names) {
            saved.append({ name, qEnvironmentVariableIsSet(name) ? std::optional(qgetenv(name)) : std::nullopt });
            qunsetenv(name);
        }
        const auto restore = qScopeGuard([&]() {
            for (const auto& [name, value] : saved) {
                if (value)
                    qputenv(name, *value);
                else
                    qunsetenv(name);
            }
        });

        QVERIFY(!Net::environmentProxy(QUrl()));

        qputenv("HTTPS_PROXY", "http://user:p%40ss@proxy.example:4444");
        const std::optional<QNetworkProxy> proxy = Net::environmentProxy(QUrl());
        QVERIFY(proxy);
        QCOMPARE(proxy->type(), QNetworkProxy::HttpProxy);
        QCOMPARE(proxy->hostName(), QStringLiteral("proxy.example"));
        QCOMPARE(proxy->port(), quint16(4444));
        QCOMPARE(proxy->user(), QStringLiteral("user"));
        QCOMPARE(proxy->password(), QStringLiteral("p@ss"));
        QCOMPARE(Net::systemProxy(QUrl(QStringLiteral("https://strm.example/a.mp3"))).hostName(),
            QStringLiteral("proxy.example"));

        qputenv("NO_PROXY", "localhost,.example");
        QVERIFY(!Net::environmentProxy(QUrl(QStringLiteral("https://strm.example/a.mp3"))));
        QVERIFY(Net::environmentProxy(QUrl(QStringLiteral("https://strm.other/a.mp3"))));

        qunsetenv("HTTPS_PROXY");
        qputenv("all_proxy", "socks5h://proxy.example");
        QCOMPARE(Net::environmentProxy(QUrl())->type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(Net::environmentProxy(QUrl())->port(), quint16(1080));
    }

    void oneMpvAdvancesToAnEnqueuedLocalTrack()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto makeWave = [&](const QString& name) {
            const QString path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return QString();
            constexpr quint32 samples = 4800; // 600 ms at 8 kHz
            QDataStream stream(&file);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.writeRawData("RIFF", 4);
            stream << quint32(36 + samples * 2);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
                   << quint16(16);
            stream.writeRawData("data", 4);
            stream << quint32(samples * 2);
            file.write(QByteArray(samples * 2, '\0'));
            return QUrl::fromLocalFile(path).toString();
        };
        const QString first = makeWave(QStringLiteral("first.wav"));
        const QString second = makeWave(QStringLiteral("second.wav"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());

        Playback::AudioPlayer player(nullptr, "null");
        QSignalSpy started(&player, &Playback::AudioPlayer::started);
        QSignalSpy errors(&player, &Playback::AudioPlayer::failed);
        bool firstEnded = false;
        connect(&player, &Playback::AudioPlayer::endOfFile, &player, [&]() {
            if (!firstEnded) {
                firstEnded = true;
                player.usePrepared(false);
            }
        });
        player.play(first, QStringLiteral("First"));
        QTRY_VERIFY(started.size() >= 1);
        player.prepare(second, QStringLiteral("Second"));
        QTRY_VERIFY(firstEnded);
        QTRY_VERIFY(started.size() >= 2);
        QCOMPARE(errors.size(), 0);
    }

    void aNetworkStreamReportsHowMuchIsBuffered()
    {
        constexpr quint32 samples = 8000 * 5; // 5 s at 8 kHz
        QByteArray wave;
        QDataStream stream(&wave, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + samples * 2);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
               << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(samples * 2);
        wave += QByteArray(samples * 2, '\0');

        QTcpServer upstream;
        QVERIFY(upstream.listen(QHostAddress::LocalHost));
        connect(&upstream, &QTcpServer::newConnection, &upstream, [&]() {
            while (QTcpSocket* socket = upstream.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &wave, request = QByteArray()]() mutable {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: "
                        + QByteArray::number(wave.size()) + "\r\nConnection: close\r\n\r\n");
                    socket->write(wave);
                    socket->disconnectFromHost();
                });
            }
        });

        Playback::AudioPlayer player(nullptr, "null");
        QList<qint64> buffered;
        connect(
            &player, &Playback::AudioPlayer::bufferedChanged, &player, [&buffered](qint64 ms) { buffered.append(ms); });
        player.play(QStringLiteral("http://127.0.0.1:%1/track.wav").arg(upstream.serverPort()), QStringLiteral("Track"),
            QNetworkProxy(QNetworkProxy::NoProxy));
        QTRY_VERIFY(!buffered.isEmpty() && buffered.last() > 1000);
        player.stop();
    }

    // A track being downloaded plays from the download's file: mpv gets
    // what's there at once and the rest as it comes, over the one
    // connection the download has.
    void aDownloadIsServedWhileItComes()
    {
        FakeUpstream upstream;
        const QByteArray body = FakeUpstream::pattern(1024 * 1024);
        upstream.bodies.insert(QStringLiteral("/track"), body);
        upstream.maxAnswerBytes = 400 * 1024;
        upstream.held.insert(QStringLiteral("/track"));

        QTemporaryDir directory;
        Playback::NetworkPool pool;
        Playback::StreamDownload download(pool, directory.filePath(QStringLiteral("track.part")));
        download.setSource({ QUrl(upstream.url(QStringLiteral("/track"))), QNetworkProxy(QNetworkProxy::NoProxy),
            { { QStringLiteral("X-Test-Token"), QStringLiteral("secret") } } });
        download.start();

        Playback::StreamRelay relay;
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QNetworkReply* reply = network.get(QNetworkRequest(relay.urlFor(&download)));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_COMPARE(upstream.requests.size(), 1);
        QTest::qWait(100);
        QCOMPARE(reply->bytesAvailable(), qint64(0)); // nothing to say yet, not even the length

        // The first range arrives, the second is held back.
        upstream.release(QStringLiteral("/track"));
        upstream.held.insert(QStringLiteral("/track"));
        QTRY_COMPARE(reply->bytesAvailable(), qint64(400 * 1024));
        QCOMPARE(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(), qint64(body.size()));
        QVERIFY(!reply->isFinished());

        upstream.release(QStringLiteral("/track"));
        QVERIFY(done.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), body);
        QVERIFY(download.isComplete());
        // The download's requests, and no other.
        QCOMPARE(upstream.requests,
            (QStringList {
                QStringLiteral("/track@0"), QStringLiteral("/track@409600"), QStringLiteral("/track@819200") }));
        // Header names are case-insensitive, and Qt 6.8+ sends them lowercase.
        for (const QByteArray& head : upstream.requestHeads)
            QVERIFY(head.toLower().contains("x-test-token: secret\r\n"));
        reply->deleteLater();
    }

    // A seek far past what's downloaded doesn't wait for the download to
    // get there.
    void aRangeTheDownloadIsNowhereNearGoesUpstream()
    {
        FakeUpstream upstream;
        const QByteArray body = FakeUpstream::pattern(2 * 1024 * 1024);
        upstream.bodies.insert(QStringLiteral("/track"), body);
        upstream.stallAfter.insert(QStringLiteral("/track"), 100 * 1024);

        QTemporaryDir directory;
        Playback::NetworkPool pool;
        Playback::StreamDownload download(pool, directory.filePath(QStringLiteral("track.part")));
        download.setSource(
            { QUrl(upstream.url(QStringLiteral("/track"))), QNetworkProxy(QNetworkProxy::NoProxy), { } });
        download.start();
        QTRY_COMPARE(download.bytesAvailable(), qint64(100 * 1024));

        Playback::StreamRelay relay;
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        const auto fetch = [&](const QByteArray& range) {
            QNetworkRequest request(relay.urlFor(&download));
            request.setRawHeader("Range", range);
            QNetworkReply* reply = network.get(request);
            QSignalSpy done(reply, &QNetworkReply::finished);
            done.wait(5000);
            reply->deleteLater();
            return std::pair(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
        };
        const qint64 far = 1536 * 1024;
        const auto ahead = fetch("bytes=" + QByteArray::number(far) + '-');
        QCOMPARE(ahead.first, 206);
        QCOMPARE(ahead.second, body.mid(far));
        QCOMPARE(upstream.requests.last(), QStringLiteral("/track@%1").arg(far));

        // What's downloaded already comes from the file.
        const auto within = fetch("bytes=1024-2047");
        QCOMPARE(within.first, 206);
        QCOMPARE(within.second, body.mid(1024, 1024));
        QCOMPARE(upstream.requests.size(), 2);
    }

    // A download that gets nowhere must not leave mpv waiting forever.
    void aDownloadGivenUpOnEndsTheRequest()
    {
        FakeUpstream upstream; // serves nothing: 404
        QTemporaryDir directory;
        Playback::NetworkPool pool;
        Playback::StreamDownload download(pool, directory.filePath(QStringLiteral("track.part")));
        download.setSource(
            { QUrl(upstream.url(QStringLiteral("/missing"))), QNetworkProxy(QNetworkProxy::NoProxy), { } });
        QSignalSpy failed(&download, &Playback::StreamDownload::failed);
        download.start();

        Playback::StreamRelay relay;
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QNetworkReply* reply = network.get(QNetworkRequest(relay.urlFor(&download)));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toBool(), false);
        QVERIFY(!reply->isFinished());
        download.giveUp();
        QVERIFY(done.wait(5000));
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 502);
        reply->deleteLater();
    }
};

QObject* makeStreamRelayTest() { return new StreamRelayTest; }

} // namespace Tests

#include "StreamRelayTest.moc"
