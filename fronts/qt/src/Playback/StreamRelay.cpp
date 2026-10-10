#include "StreamRelay.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "NetworkPool.h"
#include "StreamDownload.h"

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcRelay, "cloudmus.playback.relay")

constexpr int kMaxTargets = 16;
constexpr int kMaxRequestHeadBytes = 16 * 1024;
// No byte for this long counts as a broken connection (QNetworkRequest's
// transfer timeout covers connecting too).
constexpr int kStallTimeoutMs = 20000;
constexpr int kMaxResumeAttempts = 5;
constexpr int kResumeDelaysMs[kMaxResumeAttempts] = { 500, 1000, 2000, 4000, 5000 };
// Backpressure: Qt buffers at most this much of the upstream body, and
// more is only read while mpv's socket has less than kSocketHighWater
// queued — the relay never holds much more than a second or two of audio.
constexpr qint64 kReadBufferBytes = 256 * 1024;
constexpr qint64 kSocketHighWater = 512 * 1024;
constexpr qint64 kChunkBytes = 64 * 1024;
// A request starting this little past what's downloaded waits for the
// download to get there rather than opening a connection of its own.
constexpr qint64 kFollowSlackBytes = 512 * 1024;

} // namespace

// One mpv connection: parses its request, relays it upstream, streams the
// answer back, and resumes the body if the upstream breaks off. Owned by
// its socket; both go when mpv hangs up or the answer is complete.
class RelayConnection : public QObject {
public:
    RelayConnection(StreamRelay& relay, QTcpSocket* socket)
        : QObject(socket)
        , relay_(relay)
        , socket_(socket)
    {
        connect(socket_, &QTcpSocket::readyRead, this, &RelayConnection::onClientData);
        connect(socket_, &QTcpSocket::bytesWritten, this, &RelayConnection::pump);
        connect(socket_, &QTcpSocket::disconnected, this, [this]() {
            clientGone_ = true;
            if (reply_)
                reply_->abort();
            socket_->deleteLater();
        });
    }

private:
    void onClientData()
    {
        if (started_) {
            socket_->readAll(); // nothing more is expected from mpv
            return;
        }
        request_ += socket_->readAll();
        const qsizetype end = request_.indexOf("\r\n\r\n");
        if (end < 0) {
            if (request_.size() > kMaxRequestHeadBytes)
                respondAndClose(431, "Request Header Fields Too Large");
            return;
        }
        started_ = true;

        const QList<QByteArray> lines = request_.left(end).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        const QByteArray method = requestLine.value(0);
        QByteArray token = requestLine.value(1).mid(1); // "/<token>"
        token = token.left(token.indexOf('?') < 0 ? token.size() : token.indexOf('?'));
        const StreamRelay::Target* target = relay_.target(token);
        if (target == nullptr) {
            respondAndClose(404, "Not Found");
            return;
        }
        if (method != "GET" && method != "HEAD") {
            respondAndClose(405, "Method Not Allowed");
            return;
        }
        head_ = method == "HEAD";
        upstream_ = target->source;

        static const QRegularExpression range(QStringLiteral(R"(^\s*bytes=(\d*)-(\d*)\s*$)"));
        for (const QByteArray& line : lines.mid(1)) {
            const qsizetype colon = line.indexOf(':');
            if (colon < 0 || line.left(colon).trimmed().toLower() != "range")
                continue;
            const QRegularExpressionMatch m = range.match(QString::fromLatin1(line.mid(colon + 1)));
            if (m.hasMatch() && !m.captured(1).isEmpty()) {
                hasRange_ = true;
                rangeStart_ = m.captured(1).toLongLong();
                rangeEnd_ = m.captured(2).isEmpty() ? -1 : m.captured(2).toLongLong();
            }
        }
        if (!target->followsDownload) {
            startUpstream();
            return;
        }
        download_ = target->download;
        if (!download_) {
            respondAndClose(404, "Not Found");
            return;
        }
        following_ = true;
        StreamDownload* download = download_.data();
        connect(download, &StreamDownload::metaReady, this, &RelayConnection::followDownload);
        connect(download, &StreamDownload::progressed, this, &RelayConnection::followDownload);
        connect(download, &StreamDownload::completed, this, &RelayConnection::followDownload);
        connect(download, &StreamDownload::abandoned, this, &RelayConnection::followDownload);
        connect(download, &QObject::destroyed, this, [this]() {
            if (following_ && !clientGone_)
                socket_->disconnectFromHost();
        });
        followDownload();
    }

    // Serves the request from the file a StreamDownload is filling, waiting
    // for the bytes that aren't there yet. A request for a part the
    // download is nowhere near (a seek ahead, an index at the end of the
    // file) goes upstream on a connection of its own instead.
    void followDownload()
    {
        if (!following_ || clientGone_ || !download_)
            return;
        if (!headSent_) {
            if (!download_->hasMeta()) {
                if (download_->isAbandoned())
                    respondAndClose(502, "Bad Gateway");
                return;
            }
            const qint64 total = download_->totalLength();
            const bool farAhead
                = !download_->isComplete() && rangeStart_ > download_->bytesAvailable() + kFollowSlackBytes;
            if (total < 0 || farAhead) {
                following_ = false;
                download_->disconnect(this);
                upstream_ = download_->source();
                startUpstream();
                return;
            }
            if (rangeStart_ >= total && total > 0) {
                following_ = false;
                respondAndClose(416, "Range Not Satisfiable");
                return;
            }
            const qint64 last = rangeEnd_ >= 0 ? qMin(rangeEnd_, total - 1) : total - 1;
            expectedLength_ = qMax(qint64(0), last - rangeStart_ + 1);
            QByteArray head
                = hasRange_ ? QByteArray("HTTP/1.1 206 Partial Content\r\n") : QByteArray("HTTP/1.1 200 OK\r\n");
            if (hasRange_) {
                head += "Content-Range: bytes " + QByteArray::number(rangeStart_) + '-' + QByteArray::number(last) + '/'
                    + QByteArray::number(total) + "\r\n";
            }
            head += "Content-Length: " + QByteArray::number(expectedLength_) + "\r\nAccept-Ranges: bytes\r\n";
            if (!download_->contentType().isEmpty())
                head += "Content-Type: " + download_->contentType() + "\r\n";
            socket_->write(head + "Connection: close\r\n\r\n");
            headSent_ = true;
            if (head_) {
                following_ = false;
                socket_->disconnectFromHost();
                return;
            }
        }
        while (socket_->bytesToWrite() < kSocketHighWater && sent_ < expectedLength_) {
            const QByteArray chunk = download_->read(rangeStart_ + sent_, qMin(kChunkBytes, expectedLength_ - sent_));
            if (chunk.isEmpty())
                break;
            socket_->write(chunk);
            sent_ += chunk.size();
        }
        // All sent — or nothing more will come: mpv sees the early end and
        // reconnects, or reports the error.
        const bool starved = download_->isAbandoned() && rangeStart_ + sent_ >= download_->bytesAvailable();
        if (sent_ >= expectedLength_ || starved) {
            following_ = false;
            socket_->disconnectFromHost(); // after what's still queued is sent
        }
    }

    void startUpstream()
    {
        if (clientGone_)
            return;
        QNetworkRequest request(upstream_.url);
        request.setTransferTimeout(kStallTimeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        // Not gzip: Qt would decompress it, and the Content-Length/ranges
        // passed on to mpv would no longer match the bytes it gets.
        for (auto it = upstream_.headers.cbegin(); it != upstream_.headers.cend(); ++it)
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        request.setRawHeader("Accept-Encoding", "identity");
        const qint64 from = rangeStart_ + sent_;
        if (from > 0 || rangeEnd_ >= 0) {
            const QByteArray end = rangeEnd_ >= 0 ? QByteArray::number(rangeEnd_) : QByteArray();
            request.setRawHeader("Range", "bytes=" + QByteArray::number(from) + '-' + end);
        }
        resuming_ = headSent_;
        gotData_ = false;
        QNetworkAccessManager* network = relay_.network_->managerFor(upstream_.proxy);
        reply_ = head_ ? network->head(request) : network->get(request);
        reply_->setReadBufferSize(kReadBufferBytes);
        connect(reply_, &QNetworkReply::metaDataChanged, this, &RelayConnection::onMetaData);
        connect(reply_, &QNetworkReply::readyRead, this, &RelayConnection::pump);
        connect(reply_, &QNetworkReply::finished, this, [this]() {
            upstreamFinished_ = true;
            pump();
        });
    }

    void onMetaData()
    {
        const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || (status >= 300 && status < 400))
            return; // not the final answer yet (redirects are followed)
        if (resuming_) {
            // A resume only continues the body if the server honored the
            // Range: anything else would splice the wrong bytes in.
            if (status != 206) {
                qCWarning(lcRelay) << "can't resume" << upstream_.url.host() << "- got HTTP" << status << "to a Range";
                noResume_ = true;
                reply_->abort();
            }
            return;
        }
        if (headSent_)
            return;

        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' '
            + reply_->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray() + "\r\n";
        for (const char* name :
            { "Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "Last-Modified" }) {
            if (reply_->hasRawHeader(name))
                head += QByteArray(name) + ": " + reply_->rawHeader(name) + "\r\n";
        }
        head += "Connection: close\r\n\r\n";
        socket_->write(head);
        headSent_ = true;
        if (reply_->hasRawHeader("Content-Length"))
            expectedLength_ = reply_->rawHeader("Content-Length").toLongLong();
        // An error answer (403: the URL expired, 404, ...) is passed on as
        // is and never resumed.
        if (status >= 400)
            noResume_ = true;
    }

    void pump()
    {
        if (following_) {
            followDownload();
            return;
        }
        if (reply_ == nullptr)
            return;
        // An aborted reply is closed but still finishes: nothing left to read.
        if (headSent_ && !(resuming_ && noResume_) && reply_->isOpen()) {
            while (socket_->bytesToWrite() < kSocketHighWater && reply_->bytesAvailable() > 0) {
                const QByteArray chunk = reply_->read(kChunkBytes);
                socket_->write(chunk);
                sent_ += chunk.size();
                gotData_ = true;
            }
        }
        if (upstreamFinished_ && (!reply_->isOpen() || reply_->bytesAvailable() == 0))
            onUpstreamDone();
    }

    void onUpstreamDone()
    {
        const QNetworkReply::NetworkError error = reply_->error();
        const QString errorText = reply_->errorString();
        // Signals the HTTP thread queued before an abort still arrive
        // (metaDataChanged, readyRead) — after reply_ is gone.
        reply_->disconnect(this);
        reply_->deleteLater();
        reply_ = nullptr;
        upstreamFinished_ = false;
        if (clientGone_)
            return;

        const bool complete
            = error == QNetworkReply::NoError && (head_ || expectedLength_ < 0 || sent_ >= expectedLength_);
        if (complete) {
            socket_->disconnectFromHost(); // after what's still queued is sent
            return;
        }
        // Progress made since the last break resets the budget: a flaky
        // link that keeps delivering shouldn't run out of attempts.
        if (gotData_)
            attempts_ = 0;
        const bool resumable = !noResume_ && !head_ && (isTransientError(error) || error == QNetworkReply::NoError);
        if (resumable && attempts_ < kMaxResumeAttempts) {
            const int delay = kResumeDelaysMs[attempts_++];
            qCInfo(lcRelay) << "upstream broke off at byte" << rangeStart_ + sent_ << "(" << errorText
                            << ") — resuming in" << delay << "ms, attempt" << attempts_ << "of" << kMaxResumeAttempts;
            QTimer::singleShot(delay, this, &RelayConnection::startUpstream);
            return;
        }
        if (!headSent_) {
            qCWarning(lcRelay) << "upstream failed:" << errorText;
            respondAndClose(502, "Bad Gateway");
            return;
        }
        // Short of Content-Length: mpv sees the early end, and reconnects
        // with a Range of its own (or reports the error) as it would
        // against the server directly.
        qCWarning(lcRelay) << "giving up on" << upstream_.url.host() << "at byte" << rangeStart_ + sent_ << ":"
                           << errorText;
        socket_->disconnectFromHost();
    }

    void respondAndClose(int status, const QByteArray& reason)
    {
        socket_->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason
            + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket_->disconnectFromHost();
    }

    StreamRelay& relay_;
    QTcpSocket* socket_;
    QPointer<QNetworkReply> reply_;
    QByteArray request_;
    StreamSource upstream_;
    QPointer<StreamDownload> download_;
    // Served from download_'s file rather than from upstream.
    bool following_ = false;
    bool started_ = false;
    bool head_ = false;
    bool hasRange_ = false;
    qint64 rangeStart_ = 0;
    qint64 rangeEnd_ = -1;
    bool headSent_ = false;
    bool resuming_ = false;
    bool noResume_ = false;
    bool gotData_ = false;
    bool upstreamFinished_ = false;
    bool clientGone_ = false;
    qint64 expectedLength_ = -1;
    qint64 sent_ = 0; // body bytes written to mpv
    int attempts_ = 0;
};

StreamRelay::StreamRelay(QObject* parent)
    : QObject(parent)
    , network_(new NetworkPool(this))
{
}

QUrl StreamRelay::urlFor(const QUrl& upstream, const QNetworkProxy& proxy, const QMap<QString, QString>& headers)
{
    return addTarget(Target { StreamSource { upstream, proxy, headers }, nullptr, false });
}

QUrl StreamRelay::urlFor(StreamDownload* download) { return addTarget(Target { download->source(), download, true }); }

QUrl StreamRelay::addTarget(const Target& target)
{
    if (server_ == nullptr) {
        server_ = new QTcpServer(this);
        // Loopback only: nothing else on the network may use it.
        if (!server_->listen(QHostAddress::LocalHost, 0))
            qCWarning(lcRelay) << "can't listen:" << server_->errorString();
        connect(server_, &QTcpServer::newConnection, this, &StreamRelay::onNewConnection);
    }

    // Unguessable, so another local program can't use it to reach the
    // proxy with the user's credentials.
    QByteArray token(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(token.data()), token.size() / 4);
    token = token.toHex();
    targets_.insert(token, target);
    tokenOrder_.append(token);
    while (tokenOrder_.size() > kMaxTargets)
        targets_.remove(tokenOrder_.takeFirst());

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(server_->serverPort());
    url.setPath(QLatin1Char('/') + QString::fromLatin1(token));
    return url;
}

void StreamRelay::onNewConnection()
{
    while (QTcpSocket* socket = server_->nextPendingConnection())
        new RelayConnection(*this, socket);
}

const StreamRelay::Target* StreamRelay::target(const QByteArray& token) const
{
    const auto it = targets_.constFind(token);
    return it == targets_.constEnd() ? nullptr : &it.value();
}

} // namespace Playback
