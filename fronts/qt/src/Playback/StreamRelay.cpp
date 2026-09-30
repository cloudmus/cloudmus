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
constexpr qint64 kPrefetchBytes = 8 * 1024 * 1024;
constexpr qint64 kReadyBytes = 256 * 1024;

QString proxyKey(const QNetworkProxy& proxy)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(int(proxy.type()))
        .arg(proxy.hostName())
        .arg(proxy.port())
        .arg(proxy.user(), proxy.password());
}

bool isTransient(QNetworkReply::NetworkError error)
{
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError: // what a transfer timeout reports
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::ServiceUnavailableError:
        case QNetworkReply::InternalServerError:
            return true;
        default:
            return false;
    }
}

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
        upstream_ = target->upstream;
        proxy_ = target->proxy;
        headers_ = target->headers;
        prefix_ = target->prefix;
        prefixLength_ = target->totalLength;
        prefixComplete_ = target->complete;
        prefixType_ = target->contentType;

        static const QRegularExpression range(QStringLiteral(R"(^\s*bytes=(\d*)-(\d*)\s*$)"));
        for (const QByteArray& line : lines.mid(1)) {
            const qsizetype colon = line.indexOf(':');
            if (colon < 0 || line.left(colon).trimmed().toLower() != "range")
                continue;
            const QRegularExpressionMatch m = range.match(QString::fromLatin1(line.mid(colon + 1)));
            if (m.hasMatch() && !m.captured(1).isEmpty()) {
                rangeStart_ = m.captured(1).toLongLong();
                rangeEnd_ = m.captured(2).isEmpty() ? -1 : m.captured(2).toLongLong();
            }
        }
        if (!head_ && rangeStart_ == 0 && rangeEnd_ < 0 && !prefix_.isEmpty() && prefixLength_ >= 0) {
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(prefixLength_)
                + "\r\nAccept-Ranges: bytes\r\nConnection: close\r\n";
            if (!prefixType_.isEmpty())
                response += "Content-Type: " + prefixType_ + "\r\n";
            socket_->write(response + "\r\n");
            socket_->write(prefix_);
            sent_ = prefix_.size();
            expectedLength_ = prefixLength_;
            headSent_ = true;
            if (prefixComplete_) {
                socket_->disconnectFromHost();
                return;
            }
        }
        startUpstream();
    }

    void startUpstream()
    {
        if (clientGone_)
            return;
        QNetworkRequest request(upstream_);
        request.setTransferTimeout(kStallTimeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        // Not gzip: Qt would decompress it, and the Content-Length/ranges
        // passed on to mpv would no longer match the bytes it gets.
        for (auto it = headers_.cbegin(); it != headers_.cend(); ++it)
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        request.setRawHeader("Accept-Encoding", "identity");
        const qint64 from = rangeStart_ + sent_;
        if (from > 0 || rangeEnd_ >= 0) {
            const QByteArray end = rangeEnd_ >= 0 ? QByteArray::number(rangeEnd_) : QByteArray();
            request.setRawHeader("Range", "bytes=" + QByteArray::number(from) + '-' + end);
        }
        resuming_ = headSent_;
        gotData_ = false;
        QNetworkAccessManager* network = relay_.managerFor(proxy_);
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
                qCWarning(lcRelay) << "can't resume" << upstream_.host() << "- got HTTP" << status << "to a Range";
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
        const bool resumable = !noResume_ && !head_ && (isTransient(error) || error == QNetworkReply::NoError);
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
        qCWarning(lcRelay) << "giving up on" << upstream_.host() << "at byte" << rangeStart_ + sent_ << ":"
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
    QUrl upstream_;
    QNetworkProxy proxy_;
    QMap<QString, QString> headers_;
    QByteArray prefix_;
    QByteArray prefixType_;
    qint64 prefixLength_ = -1;
    bool prefixComplete_ = false;
    bool started_ = false;
    bool head_ = false;
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
{
}

QUrl StreamRelay::urlFor(const QUrl& upstream, const QNetworkProxy& proxy, const QMap<QString, QString>& headers)
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
    targets_.insert(token, Target { upstream, proxy, headers });
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

QUrl StreamRelay::prefetch(const QUrl& upstream, const QNetworkProxy& proxy, const QMap<QString, QString>& headers)
{
    cancelPrefetch();
    const QUrl local = urlFor(upstream, proxy, headers);
    prefetchToken_ = local.path().mid(1).toLatin1();
    Target& target = targets_[prefetchToken_];
    target.headers = headers;

    QNetworkRequest request(upstream);
    request.setTransferTimeout(kStallTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Range", "bytes=0-" + QByteArray::number(kPrefetchBytes - 1));
    prefetchReply_ = managerFor(proxy)->get(request);
    prefetchReply_->setReadBufferSize(kReadBufferBytes);
    QNetworkReply* reply = prefetchReply_;
    const QByteArray token = prefetchToken_;
    const auto collect = [this, reply, token, local]() {
        if (prefetchReply_ != reply || !targets_.contains(token))
            return;
        Target& target = targets_[token];
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206)
            return;
        const QByteArray range = reply->rawHeader("Content-Range");
        if (status == 206 && range.startsWith("bytes 0-")) {
            const qsizetype slash = range.lastIndexOf('/');
            bool ok = false;
            const qint64 total = range.mid(slash + 1).toLongLong(&ok);
            if (ok)
                target.totalLength = total;
        } else if (status == 200) {
            bool ok = false;
            const qint64 total = reply->rawHeader("Content-Length").toLongLong(&ok);
            if (ok && total <= kPrefetchBytes) {
                target.totalLength = total;
                target.complete = true;
            }
        }
        target.contentType = reply->rawHeader("Content-Type");
        if (reply->isOpen())
            target.prefix += reply->read(kPrefetchBytes - target.prefix.size());
        if (reply->isFinished() && target.totalLength == target.prefix.size())
            target.complete = true;
        if (target.prefix.size() >= kPrefetchBytes && !reply->isFinished())
            reply->abort();
        const bool usable = target.totalLength >= 0 && (status == 206 || target.complete);
        const bool filled = target.prefix.size() >= kReadyBytes || reply->isFinished();
        if (usable && filled && !target.prefix.isEmpty() && !target.ready
            && (!target.complete || target.prefix.size() == target.totalLength)) {
            target.ready = true;
            emit prefetchReady(local);
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, collect);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collect]() {
        collect();
        if (prefetchReply_ == reply)
            prefetchReply_ = nullptr;
        reply->deleteLater();
    });
    return local;
}

void StreamRelay::cancelPrefetch()
{
    if (prefetchReply_) {
        prefetchReply_->disconnect(this);
        prefetchReply_->abort();
        prefetchReply_->deleteLater();
        prefetchReply_ = nullptr;
    }
    if (!prefetchToken_.isEmpty()) {
        auto it = targets_.find(prefetchToken_);
        if (it != targets_.end())
            it->prefix.clear();
    }
    prefetchToken_.clear();
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

QNetworkAccessManager* StreamRelay::managerFor(const QNetworkProxy& proxy)
{
    const QString key = proxyKey(proxy);
    QNetworkAccessManager*& manager = managers_[key];
    if (manager == nullptr) {
        manager = new QNetworkAccessManager(this);
        manager->setProxy(proxy);
    }
    return manager;
}

} // namespace Playback
