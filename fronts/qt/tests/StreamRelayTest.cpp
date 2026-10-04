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
#include "ProxyRouting.h"
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
        player.prepare(second, QStringLiteral("Second"), std::nullopt);
        QTRY_VERIFY(firstEnded);
        QTRY_VERIFY(started.size() >= 2);
        QCOMPARE(errors.size(), 0);
    }

    void aTrackPreparedWhileAProxiedOneIsPlayingGetsQueued()
    {
        // play() through a route creates the relay first; the prefetch
        // prepare() then starts through the same relay must still report.
        const QByteArray body(512 * 1024, 'x');
        QTcpServer upstream;
        QVERIFY(upstream.listen(QHostAddress::LocalHost));
        connect(&upstream, &QTcpServer::newConnection, &upstream, [&]() {
            while (QTcpSocket* socket = upstream.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &body, request = QByteArray()]() mutable {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    const QByteArray total = QByteArray::number(body.size());
                    socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"
                        + QByteArray::number(body.size() - 1) + '/' + total + "\r\nContent-Length: " + total
                        + "\r\nConnection: close\r\n\r\n");
                    socket->write(body);
                    socket->disconnectFromHost();
                });
            }
        });

        Playback::AudioPlayer player(nullptr, "null");
        QSignalSpy prepared(&player, &Playback::AudioPlayer::prepared);
        const QNetworkProxy direct(QNetworkProxy::NoProxy);
        player.play(QStringLiteral("http://127.0.0.1:%1/current").arg(upstream.serverPort()), QStringLiteral("Current"),
            direct);
        player.prepare(
            QStringLiteral("http://127.0.0.1:%1/next").arg(upstream.serverPort()), QStringLiteral("Next"), direct);
        QVERIFY(prepared.wait(5000));
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

    void stoppingDuringRedirectPreflightCannotStartTheOldTrack()
    {
        QTcpServer upstream;
        QVERIFY(upstream.listen(QHostAddress::LocalHost));
        QTcpSocket* pending = nullptr;
        bool sawHead = false;
        connect(&upstream, &QTcpServer::newConnection, &upstream, [&]() {
            pending = upstream.nextPendingConnection();
            connect(pending, &QTcpSocket::readyRead, pending,
                [pending, &sawHead]() { sawHead = pending->readAll().startsWith("HEAD "); });
        });

        Playback::AudioPlayer player;
        QSignalSpy started(&player, &Playback::AudioPlayer::started);
        const QString url = QStringLiteral("http://127.0.0.1:%1/slow").arg(upstream.serverPort());
        player.play(url, QStringLiteral("Slow track"));
        QTRY_VERIFY(sawHead);
        player.stop();
        pending->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        pending->disconnectFromHost();
        QTest::qWait(100);
        QCOMPARE(started.size(), 0);
    }

    void aPrefetchedPrefixIsServedBeforeTheRemainingRange()
    {
        QByteArray body(1024 * 1024, '\0');
        for (qsizetype i = 0; i < body.size(); ++i)
            body[i] = char('a' + i % 26);

        QTcpServer upstream;
        QVERIFY(upstream.listen(QHostAddress::LocalHost));
        QList<qint64> requestedStarts;
        bool headersForwarded = true;
        connect(&upstream, &QTcpServer::newConnection, &upstream, [&]() {
            while (QTcpSocket* socket = upstream.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, &body, &requestedStarts, &headersForwarded, request = QByteArray()]() mutable {
                        request += socket->readAll();
                        if (!request.contains("\r\n\r\n"))
                            return;
                        headersForwarded = headersForwarded && request.contains("X-Test-Token: secret\r\n");
                        const QRegularExpression pattern(
                            QStringLiteral(R"(Range: bytes=(\d+)-)"), QRegularExpression::CaseInsensitiveOption);
                        const auto match = pattern.match(QString::fromLatin1(request));
                        const qint64 start = match.hasMatch() ? match.captured(1).toLongLong() : 0;
                        requestedStarts.append(start);
                        const qint64 end = qMin(start + 512 * 1024, qint64(body.size())) - 1;
                        const QByteArray chunk = body.mid(start, end - start + 1);
                        const QByteArray response = "HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\n"
                                                    "Content-Range: bytes "
                            + QByteArray::number(start) + '-' + QByteArray::number(end) + '/'
                            + QByteArray::number(body.size()) + "\r\nContent-Length: "
                            + QByteArray::number(chunk.size()) + "\r\nConnection: close\r\n\r\n";
                        socket->write(response);
                        socket->write(chunk);
                        socket->disconnectFromHost();
                    });
            }
        });

        Playback::StreamRelay relay;
        QSignalSpy ready(&relay, &Playback::StreamRelay::prefetchReady);
        const QUrl source(QStringLiteral("http://127.0.0.1:%1/track").arg(upstream.serverPort()));
        const QUrl local = relay.prefetch(source, QNetworkProxy(QNetworkProxy::NoProxy),
            { { QStringLiteral("X-Test-Token"), QStringLiteral("secret") } });
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.first().first().toUrl(), local);

        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QNetworkReply* reply = network.get(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), body);
        QCOMPARE(requestedStarts, (QList<qint64> { 0, 512 * 1024 }));
        QVERIFY(headersForwarded);
        reply->deleteLater();
    }
};

QObject* makeStreamRelayTest() { return new StreamRelayTest; }

} // namespace Tests

#include "StreamRelayTest.moc"
