#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

namespace Tests {

// A stream server on loopback for the download tests: serves `bodies` by
// path, honoring Range, and can be made to misbehave per path.
class FakeUpstream : public QObject {
public:
    FakeUpstream()
    {
        server_.listen(QHostAddress::LocalHost);
        connect(&server_, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request = QByteArray()]() mutable {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    const QByteArray head = std::exchange(request, { });
                    onRequest(socket, head);
                });
            }
        });
    }

    QString url(const QString& path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path);
    }

    static QByteArray pattern(qsizetype size, char seed = 'a')
    {
        QByteArray body(size, '\0');
        for (qsizetype i = 0; i < size; ++i)
            body[i] = char(seed + i % 23);
        return body;
    }

    // A WAV of silence: something mpv plays, and nobody hears.
    static QByteArray silence(int seconds)
    {
        const quint32 samples = 8000 * quint32(seconds);
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
        return wave + QByteArray(samples * 2, '\0');
    }

    // Answers the requests for `path` that were held back, and stops
    // holding new ones.
    void release(const QString& path)
    {
        held.remove(path);
        const QList<Pending> pending = std::exchange(pending_, { });
        for (const Pending& p : pending) {
            if (p.path != path)
                pending_.append(p);
            else if (p.socket)
                answer(p.socket, p.path, p.start, p.end);
        }
    }

    QHash<QString, QByteArray> bodies;
    // Requests for these get no answer until release().
    QSet<QString> held;
    // These answer 403.
    QSet<QString> forbidden;
    // The next answer for the path stops after this many body bytes, the
    // connection left open.
    QHash<QString, qint64> stallAfter;
    // The next answer for the path is cut off after this many body bytes:
    // the connection closes.
    QHash<QString, qint64> cutAfter;
    // These ignore Range: 200 and the whole body.
    QSet<QString> wholeOnly;
    // These don't say their length: "bytes 0-99/*".
    QSet<QString> lengthUnknown;
    // No answer carries more body than this, whatever range was asked for.
    qint64 maxAnswerBytes = -1;
    // "<path>@<first byte>", in the order they came.
    QStringList requests;
    QList<QByteArray> requestHeads;

private:
    struct Pending {
        QPointer<QTcpSocket> socket;
        QString path;
        qint64 start;
        qint64 end;
    };

    void onRequest(QTcpSocket* socket, const QByteArray& head)
    {
        requestHeads.append(head);
        const QString path = QString::fromLatin1(head.left(head.indexOf('\r')).split(' ').value(1));
        static const QRegularExpression range(
            QStringLiteral(R"(Range: bytes=(\d+)-(\d*))"), QRegularExpression::CaseInsensitiveOption);
        const auto match = range.match(QString::fromLatin1(head));
        const qint64 start = match.hasMatch() ? match.captured(1).toLongLong() : 0;
        const qint64 end = match.hasMatch() && !match.captured(2).isEmpty() ? match.captured(2).toLongLong() : -1;
        requests.append(QStringLiteral("%1@%2").arg(path).arg(start));
        if (held.contains(path)) {
            pending_.append(Pending { socket, path, start, end });
            return;
        }
        answer(socket, path, start, end);
    }

    void answer(QTcpSocket* socket, const QString& path, qint64 start, qint64 end)
    {
        if (forbidden.contains(path)) {
            socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        if (!bodies.contains(path)) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QByteArray& body = bodies[path];
        if (wholeOnly.contains(path)) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
            socket->write(body);
            socket->disconnectFromHost();
            return;
        }
        if (start >= body.size()) {
            socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        qint64 last = end < 0 ? body.size() - 1 : qMin(end, qint64(body.size()) - 1);
        if (maxAnswerBytes > 0)
            last = qMin(last, start + maxAnswerBytes - 1);
        const QByteArray chunk = body.mid(start, last - start + 1);
        socket->write("HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Range: bytes "
            + QByteArray::number(start) + '-' + QByteArray::number(last) + '/'
            + (lengthUnknown.contains(path) ? QByteArray("*") : QByteArray::number(body.size()))
            + "\r\nContent-Length: " + QByteArray::number(chunk.size()) + "\r\nConnection: close\r\n\r\n");
        if (cutAfter.contains(path)) {
            socket->write(chunk.left(cutAfter.take(path)));
            socket->disconnectFromHost();
            return;
        }
        if (stallAfter.contains(path)) {
            socket->write(chunk.left(stallAfter.take(path)));
            return;
        }
        socket->write(chunk);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
    QList<Pending> pending_;
};

} // namespace Tests
