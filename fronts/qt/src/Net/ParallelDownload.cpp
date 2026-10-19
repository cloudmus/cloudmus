#include "ParallelDownload.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>

#include "NetworkErrors.h"

namespace Net {

namespace {

Q_LOGGING_CATEGORY(lcDownload, "cloudmus.net.download")

// No byte for this long counts as a broken connection (QNetworkRequest's
// transfer timeout covers connecting too).
constexpr int kStallTimeoutMs = 20000;
constexpr int kMaxResumeAttempts = 5;
constexpr int kResumeDelaysMs[kMaxResumeAttempts] = { 500, 1000, 2000, 4000, 5000 };
// Before the first byte there is nothing to lose by giving up early: a
// stream is resolved again instead (see PlaybackController's start
// retries).
constexpr int kMaxStartAttempts = 2;

// Without the query: a signed stream URL's token has no place in the log,
// which crash reports carry along.
QString forLog(const QUrl& url)
{
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveUserInfo | QUrl::RemoveFragment);
}

QString proxyForLog(const QNetworkAccessManager& network)
{
    const QNetworkProxy proxy = network.proxy();
    switch (proxy.type()) {
        case QNetworkProxy::NoProxy:
            return QStringLiteral("direct");
        case QNetworkProxy::DefaultProxy:
            return QStringLiteral("default proxy");
        default:
            return QStringLiteral("proxy %1:%2").arg(proxy.hostName()).arg(proxy.port());
    }
}

double mibPerSecond(qint64 bytes, qint64 ms) { return double(bytes) / 1048.576 / double(qMax<qint64>(ms, 1)); }

} // namespace

ParallelDownload::ParallelDownload(const QString& filePath, QObject* parent)
    : QObject(parent)
    , file_(filePath)
{
}

ParallelDownload::~ParallelDownload() { dropReplies(); }

void ParallelDownload::setSource(QNetworkAccessManager* network, const QUrl& url, const QMap<QString, QString>& headers)
{
    network_ = network;
    url_ = url;
    headers_ = headers;
}

void ParallelDownload::setConnections(int connections)
{
    connections_ = qMax(connections, 1);
    if (isActive())
        pump();
}

void ParallelDownload::start()
{
    if (complete_ || isActive())
        return;
    failed_ = false;
    failedStatus_ = 0;
    errorString_.clear();
    for (Range& range : ranges_)
        range.attempts = 0;
    if (ranges_.isEmpty())
        ranges_.append(Range { });
    if (!openFile())
        return;
    clock_.start();
    receivedAtStart_ = received_;
    for (Range& range : ranges_)
        range.startedMs = -1;
    qCDebug(lcDownload).noquote() << (received_ > 0 ? "resuming" : "starting") << forLog(url_) << "into"
                                  << file_.fileName() << "| connections:" << connections_ << "| range:" << rangeBytes_
                                  << "bytes | from byte:" << received_ << "| total:" << total_ << "|"
                                  << (network_ != nullptr ? proxyForLog(*network_) : QStringLiteral("no network"));
    pump();
}

void ParallelDownload::pause()
{
    if (isActive())
        qCDebug(lcDownload).noquote() << "pausing" << forLog(url_) << "at" << received_ << "of" << total_ << "bytes,"
                                      << contiguous_ << "from the start";
    ++generation_;
    for (Range& range : ranges_)
        range.retryPending = false;
    dropReplies();
}

void ParallelDownload::reset()
{
    pause();
    file_.close();
    ranges_.clear();
    wholeBody_ = false;
    contentType_.clear();
    received_ = 0;
    contiguous_ = 0;
    total_ = -1;
    hasMeta_ = false;
    complete_ = false;
    failed_ = false;
    failedStatus_ = 0;
    errorString_.clear();
}

bool ParallelDownload::isActive() const
{
    for (const Range& range : ranges_) {
        if (range.isBusy())
            return true;
    }
    return false;
}

int ParallelDownload::activeConnections() const
{
    return int(std::count_if(ranges_.cbegin(), ranges_.cend(), [](const Range& r) { return r.reply != nullptr; }));
}

void ParallelDownload::relocate(const QString& filePath)
{
    file_.close();
    file_.setFileName(filePath);
}

bool ParallelDownload::openFile()
{
    if (file_.isOpen())
        return true;
    // Unbuffered: a reader goes through a handle of its own and must see
    // every byte as soon as it's written.
    const auto mode = received_ == 0 ? QIODevice::ReadWrite | QIODevice::Truncate : QIODevice::ReadWrite;
    if (file_.open(mode | QIODevice::Unbuffered))
        return true;
    qCWarning(lcDownload) << "can't write" << file_.fileName() << ":" << file_.errorString();
    fail(false, 0, file_.errorString());
    return false;
}

void ParallelDownload::dropReplies()
{
    for (Range& range : ranges_) {
        if (QNetworkReply* reply = range.reply) {
            range.reply = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
}

void ParallelDownload::pump()
{
    if (complete_ || failed_ || network_ == nullptr)
        return;
    // Until the length is known — or when the server ignores Range — there
    // is only the one request to make.
    const int allowed = total_ < 0 || wholeBody_ ? 1 : connections_;
    qsizetype busy = std::count_if(ranges_.cbegin(), ranges_.cend(), [](const Range& r) { return r.isBusy(); });
    // The earliest first: the file fills from its start.
    for (qsizetype i = 0; i < ranges_.size() && busy < allowed; ++i) {
        Range& range = ranges_[i];
        if (range.isBusy() || range.isComplete())
            continue;
        request(range);
        ++busy;
    }
}

void ParallelDownload::request(Range& range)
{
    QNetworkRequest request(url_);
    request.setTransferTimeout(kStallTimeoutMs);
    // GitHub serves release assets through a redirect to its storage.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    for (auto it = headers_.cbegin(); it != headers_.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    // Not gzip: Qt would decompress it, and the lengths and ranges would no
    // longer match the bytes.
    request.setRawHeader("Accept-Encoding", "identity");
    if (!wholeBody_) {
        const qint64 first = range.next();
        const qint64 end = range.end >= 0 ? qMin(range.end, first + rangeBytes_) : first + rangeBytes_;
        request.setRawHeader("Range", "bytes=" + QByteArray::number(first) + '-' + QByteArray::number(end - 1));
        const QString attempt = range.attempts > 0 ? QStringLiteral(", attempt %1").arg(range.attempts + 1) : QString();
        qCDebug(lcDownload).nospace().noquote()
            << "requesting bytes " << first << '-' << end - 1 << " of " << url_.host() << " (connection "
            << activeConnections() + 1 << " of " << connections_ << attempt << ')';
    } else {
        qCDebug(lcDownload).noquote() << "requesting the whole body of" << url_.host();
    }
    if (range.startedMs < 0)
        range.startedMs = clock_.elapsed();
    range.httpStatus = 0;
    range.statusSeen = false;
    range.gotData = false;
    QNetworkReply* reply = network_->get(request);
    range.reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() { onMetaData(reply); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { onData(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onFinished(reply); });
}

ParallelDownload::Range* ParallelDownload::rangeOf(QNetworkReply* reply)
{
    for (Range& range : ranges_) {
        if (range.reply == reply)
            return &range;
    }
    return nullptr;
}

void ParallelDownload::onMetaData(QNetworkReply* reply)
{
    Range* range = rangeOf(reply);
    if (range == nullptr)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0 || (status >= 300 && status < 400) || range->statusSeen)
        return; // not the final answer yet (redirects are followed)
    range->statusSeen = true;
    range->httpStatus = status;
    if (status != 200 && status != 206)
        return;

    if (status == 206) {
        const QByteArray contentRange = reply->rawHeader("Content-Range"); // "bytes 0-99/1234"
        const qsizetype slash = contentRange.lastIndexOf('/');
        bool totalOk = false;
        const qint64 total = slash < 0 ? -1 : contentRange.mid(slash + 1).trimmed().toLongLong(&totalOk);
        const qsizetype dash = contentRange.indexOf('-');
        bool ok = false;
        const qint64 first = contentRange.mid(6, dash - 6).trimmed().toLongLong(&ok);
        if (!contentRange.startsWith("bytes ") || !ok || first != range->next()) {
            // Not the bytes asked for: splicing them in would corrupt the file.
            qCWarning(lcDownload) << "unexpected range from" << url_.host() << ":" << contentRange;
            range->httpStatus = 0;
            reply->abort();
            return;
        }
        if (!hasMeta_ && reply->url().host() != url_.host())
            qCDebug(lcDownload).noquote() << url_.host() << "redirected to" << reply->url().host();
        if (totalOk && total_ < 0)
            lengthKnown(total); // `range` is stale from here on
    } else {
        qCDebug(lcDownload).noquote() << url_.host() << "ignores Range: one request for the whole body of"
                                      << reply->rawHeader("Content-Length") << "bytes";
        takeWholeBody(reply);
    }
    contentType_ = reply->rawHeader("Content-Type");
    if (!hasMeta_) {
        hasMeta_ = true;
        emit metaReady();
    }
    pump();
}

void ParallelDownload::lengthKnown(qint64 total)
{
    total_ = total;
    if (ranges_.size() != 1 || ranges_.first().end >= 0)
        return;
    // The first range ends where it was asked to; the rest is cut up for
    // the other connections.
    Range& first = ranges_.first();
    first.end = qMin(total, first.next() + rangeBytes_);
    for (qint64 start = first.end; start < total; start += rangeBytes_)
        ranges_.append(Range { start, qMin(total, start + rangeBytes_) });
    qCDebug(lcDownload).noquote() << url_.host() << "has" << total << "bytes:" << ranges_.size() << "ranges over"
                                  << connections_ << "connections";
}

void ParallelDownload::takeWholeBody(QNetworkReply* reply)
{
    // The whole body, whatever range was asked for: it replaces everything
    // there is, and every other request.
    wholeBody_ = true;
    for (Range& range : ranges_) {
        if (range.reply == reply)
            range.reply = nullptr;
    }
    pause();
    Range whole;
    whole.reply = reply;
    whole.statusSeen = true;
    whole.httpStatus = 200;
    bool ok = false;
    const qint64 length = reply->rawHeader("Content-Length").toLongLong(&ok);
    total_ = ok ? length : -1;
    whole.end = total_;
    ranges_ = { whole };
    if (received_ > 0) {
        received_ = 0;
        contiguous_ = 0;
        file_.resize(0);
    }
}

void ParallelDownload::onData(QNetworkReply* reply)
{
    Range* range = rangeOf(reply);
    if (range == nullptr || !reply->isOpen() || (range->httpStatus != 200 && range->httpStatus != 206))
        return;
    QByteArray data = reply->readAll();
    // More than the range was meant to hold: the next one has the rest.
    if (range->end >= 0 && range->next() + data.size() > range->end)
        data.truncate(range->end - range->next());
    if (data.isEmpty())
        return;
    if (!file_.seek(range->next()) || file_.write(data) != data.size()) {
        qCWarning(lcDownload) << "can't write" << file_.fileName() << ":" << file_.errorString();
        fail(false, 0, file_.errorString());
        return;
    }
    range->done += data.size();
    range->gotData = true;
    received_ += data.size();
    updateContiguous();
    emit progressed();
}

void ParallelDownload::onFinished(QNetworkReply* reply)
{
    onData(reply);
    Range* range = rangeOf(reply);
    if (range == nullptr)
        return; // a write failed
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const int status = range->httpStatus;
    range->reply = nullptr;
    reply->disconnect(this);
    reply->deleteLater();

    if (status == 403 || status == 410) {
        qCInfo(lcDownload) << url_.host() << "no longer accepts the URL: HTTP" << status;
        fail(true, status, errorText);
        return;
    }
    // Past the end of a file whose length nobody said.
    if (status == 416 && received_ > 0 && total_ < 0) {
        range->end = range->next();
    } else if (status >= 400 && status < 500) {
        qCWarning(lcDownload) << url_.host() << "answered HTTP" << status;
        fail(false, status, errorText);
        return;
    } else if (error == QNetworkReply::NoError && (status == 200 || status == 206)) {
        // With no length to go by, the end is where the bytes stop.
        if (total_ < 0 && (wholeBody_ || !range->gotData))
            range->end = range->next();
        if (!range->isComplete()) {
            if (range->gotData && !wholeBody_) {
                range->attempts = 0;
                request(*range); // the rest of the range, without a pause
                return;
            }
            // Ended short of its length: try to pick it up with a range.
            wholeBody_ = false;
            qCInfo(lcDownload) << "download ended short at byte" << range->next();
            retryOrFail(*range, errorText);
            return;
        }
    } else if (error != QNetworkReply::NoError && !isTransientError(error) && status < 500) {
        qCWarning(lcDownload) << "download failed:" << errorText;
        fail(false, status, errorText);
        return;
    } else {
        qCInfo(lcDownload) << "download broke off at byte" << range->next() << "(" << errorText << ")";
        retryOrFail(*range, errorText);
        return;
    }

    if (range->isComplete()) {
        const qint64 ms = clock_.elapsed() - range->startedMs;
        qCDebug(lcDownload).nospace().noquote()
            << "bytes " << range->start << '-' << range->end - 1 << " of " << url_.host() << " done in " << ms
            << " ms (" << mibPerSecond(range->done, ms) << " MiB/s); " << received_ << " of " << total_
            << " bytes so far";
    }
    updateContiguous();
    if (!std::all_of(ranges_.cbegin(), ranges_.cend(), [](const Range& r) { return r.isComplete(); }))
        pump();
    else if (!isActive())
        finish(); // the last of the replies — others may have all their bytes but not their end yet
}

void ParallelDownload::retryOrFail(Range& range, const QString& error)
{
    // Progress made since the last break resets the budget: a flaky link
    // that keeps delivering shouldn't run out of attempts.
    if (range.gotData)
        range.attempts = 0;
    const int limit = received_ == 0 ? kMaxStartAttempts : kMaxResumeAttempts;
    if (range.attempts >= limit) {
        fail(false, range.httpStatus, error);
        return;
    }
    range.retryPending = true;
    QTimer::singleShot(
        kResumeDelaysMs[range.attempts++], this, [this, start = range.start, generation = generation_]() {
            if (generation != generation_)
                return;
            for (Range& r : ranges_) {
                if (r.start == start && r.retryPending) {
                    r.retryPending = false;
                    request(r);
                    return;
                }
            }
        });
}

void ParallelDownload::updateContiguous()
{
    qint64 contiguous = 0;
    for (const Range& range : ranges_) {
        contiguous = range.next();
        if (!range.isComplete())
            break;
    }
    contiguous_ = contiguous;
}

void ParallelDownload::finish()
{
    file_.close();
    total_ = received_;
    contiguous_ = received_;
    complete_ = true;
    const qint64 ms = clock_.elapsed();
    const qint64 bytes = received_ - receivedAtStart_;
    qCDebug(lcDownload).nospace().noquote()
        << "downloaded " << bytes << " bytes of " << forLog(url_) << " in " << ms << " ms: " << mibPerSecond(bytes, ms)
        << " MiB/s over up to " << (wholeBody_ ? 1 : connections_) << " connections, " << ranges_.size() << " ranges";
    if (!hasMeta_) {
        hasMeta_ = true;
        emit metaReady();
    }
    emit completed();
}

void ParallelDownload::fail(bool expired, int status, const QString& error)
{
    qCDebug(lcDownload).noquote() << "giving up on" << forLog(url_) << "at" << received_ << "of" << total_
                                  << "bytes | expired:" << expired << "| HTTP" << status << "|" << error;
    pause();
    failed_ = true;
    failedStatus_ = status;
    errorString_ = error;
    emit failed(expired);
}

} // namespace Net
