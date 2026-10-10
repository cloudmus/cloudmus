#include "StreamDownload.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "NetworkPool.h"

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcDownload, "cloudmus.playback.download")

// Under the ~10 MB above which YouTube throttles a single request.
constexpr qint64 kRangeBytes = 8 * 1024 * 1024;
// No byte for this long counts as a broken connection (QNetworkRequest's
// transfer timeout covers connecting too).
constexpr int kStallTimeoutMs = 20000;
constexpr int kMaxResumeAttempts = 5;
constexpr int kResumeDelaysMs[kMaxResumeAttempts] = { 500, 1000, 2000, 4000, 5000 };
// Before the first byte there is nothing to lose by giving up early: the
// track is resolved again instead (see PlaybackController's start retries).
constexpr int kMaxStartAttempts = 2;

} // namespace

StreamDownload::StreamDownload(NetworkPool& network, const QString& filePath, QObject* parent)
    : QObject(parent)
    , network_(network)
    , filePath_(filePath)
    , file_(filePath)
    , retryTimer_(new QTimer(this))
{
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, [this]() {
        retryPending_ = false;
        request();
    });
}

StreamDownload::~StreamDownload() { dropReply(); }

void StreamDownload::start()
{
    if (complete_ || isActive())
        return;
    failed_ = false;
    attempts_ = 0;
    request();
}

void StreamDownload::pause()
{
    retryTimer_->stop();
    retryPending_ = false;
    dropReply();
}

void StreamDownload::giveUp()
{
    pause();
    if (abandoned_ || complete_)
        return;
    abandoned_ = true;
    emit abandoned();
}

void StreamDownload::dropReply()
{
    if (!reply_)
        return;
    reply_->disconnect(this);
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
}

void StreamDownload::request()
{
    if (!file_.isOpen()) {
        // Unbuffered: read() goes through a handle of its own and must see
        // every byte as soon as it's written.
        const auto mode
            = received_ == 0 ? QIODevice::WriteOnly | QIODevice::Truncate : QIODevice::WriteOnly | QIODevice::Append;
        if (!file_.open(mode | QIODevice::Unbuffered)) {
            qCWarning(lcDownload) << "can't write" << filePath_ << ":" << file_.errorString();
            fail(false);
            return;
        }
    }

    QNetworkRequest request(source_.url);
    request.setTransferTimeout(kStallTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    for (auto it = source_.headers.cbegin(); it != source_.headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    // Not gzip: Qt would decompress it, and the lengths and ranges would no
    // longer match the bytes.
    request.setRawHeader("Accept-Encoding", "identity");
    if (!wholeBody_) {
        request.setRawHeader(
            "Range", "bytes=" + QByteArray::number(received_) + '-' + QByteArray::number(received_ + kRangeBytes - 1));
    }
    httpStatus_ = 0;
    statusSeen_ = false;
    gotData_ = false;
    reply_ = network_.managerFor(source_.proxy)->get(request);
    connect(reply_, &QNetworkReply::metaDataChanged, this, &StreamDownload::onMetaData);
    connect(reply_, &QNetworkReply::readyRead, this, &StreamDownload::onData);
    connect(reply_, &QNetworkReply::finished, this, &StreamDownload::onFinished);
}

void StreamDownload::onMetaData()
{
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0 || (status >= 300 && status < 400) || statusSeen_)
        return; // not the final answer yet (redirects are followed)
    statusSeen_ = true;
    httpStatus_ = status;
    if (status != 200 && status != 206)
        return;

    if (status == 206) {
        const QByteArray range = reply_->rawHeader("Content-Range"); // "bytes 0-99/1234"
        const qsizetype slash = range.lastIndexOf('/');
        bool ok = false;
        const qint64 total = slash < 0 ? -1 : range.mid(slash + 1).trimmed().toLongLong(&ok);
        if (ok)
            total_ = total;
        const qsizetype dash = range.indexOf('-');
        const qint64 first = range.mid(6, dash - 6).trimmed().toLongLong(&ok);
        if (!range.startsWith("bytes ") || !ok || first != received_) {
            // Not the bytes asked for: splicing them in would corrupt the file.
            qCWarning(lcDownload) << "unexpected range from" << source_.url.host() << ":" << range;
            httpStatus_ = 0;
            reply_->abort();
            return;
        }
    } else {
        // The whole body, whatever range was asked for.
        wholeBody_ = true;
        if (received_ > 0) {
            received_ = 0;
            file_.resize(0);
        }
        bool ok = false;
        const qint64 total = reply_->rawHeader("Content-Length").toLongLong(&ok);
        total_ = ok ? total : -1;
    }
    contentType_ = reply_->rawHeader("Content-Type");
    if (!hasMeta_) {
        hasMeta_ = true;
        emit metaReady();
    }
}

void StreamDownload::onData()
{
    if (!reply_ || !reply_->isOpen() || (httpStatus_ != 200 && httpStatus_ != 206))
        return;
    const QByteArray data = reply_->readAll();
    if (data.isEmpty())
        return;
    if (file_.write(data) != data.size()) {
        qCWarning(lcDownload) << "can't write" << filePath_ << ":" << file_.errorString();
        dropReply();
        fail(false);
        return;
    }
    received_ += data.size();
    gotData_ = true;
    emit progressed();
}

void StreamDownload::onFinished()
{
    onData();
    if (!reply_)
        return; // a write failed
    const QNetworkReply::NetworkError error = reply_->error();
    const QString errorText = reply_->errorString();
    const int status = httpStatus_;
    reply_->disconnect(this);
    reply_->deleteLater();
    reply_ = nullptr;

    if (status == 403 || status == 410) {
        qCInfo(lcDownload) << source_.url.host() << "no longer accepts the URL: HTTP" << status;
        fail(true);
        return;
    }
    // Past the end of a stream whose length nobody said.
    if (status == 416 && received_ > 0 && total_ < 0) {
        finish();
        return;
    }
    if (status >= 400 && status < 500) {
        qCWarning(lcDownload) << source_.url.host() << "answered HTTP" << status;
        fail(false);
        return;
    }
    if (error == QNetworkReply::NoError && (status == 200 || status == 206)) {
        if (total_ >= 0 ? received_ >= total_ : (wholeBody_ || !gotData_)) {
            finish();
            return;
        }
        if (gotData_ && !wholeBody_) {
            attempts_ = 0;
            request(); // the next range, without a pause
            return;
        }
        // Ended short of its length: try to pick it up with a range.
        wholeBody_ = false;
    } else if (error != QNetworkReply::NoError && !isTransientError(error) && status < 500) {
        qCWarning(lcDownload) << "download failed:" << errorText;
        fail(false);
        return;
    }
    qCInfo(lcDownload) << "download broke off at byte" << received_ << "(" << errorText << ")";
    retryOrFail();
}

void StreamDownload::retryOrFail()
{
    // Progress made since the last break resets the budget: a flaky link
    // that keeps delivering shouldn't run out of attempts.
    if (gotData_)
        attempts_ = 0;
    const int limit = received_ == 0 ? kMaxStartAttempts : kMaxResumeAttempts;
    if (attempts_ >= limit) {
        fail(false);
        return;
    }
    retryPending_ = true;
    retryTimer_->start(kResumeDelaysMs[attempts_++]);
}

void StreamDownload::finish()
{
    file_.close();
    total_ = received_;
    complete_ = true;
    if (!hasMeta_) {
        hasMeta_ = true;
        emit metaReady();
    }
    emit completed();
}

void StreamDownload::fail(bool expired)
{
    failed_ = true;
    emit failed(expired);
}

QByteArray StreamDownload::read(qint64 offset, qint64 maxBytes)
{
    if (offset >= received_ || maxBytes <= 0)
        return { };
    if (!readFile_.isOpen()) {
        readFile_.setFileName(filePath_);
        if (!readFile_.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
            return { };
    }
    if (!readFile_.seek(offset))
        return { };
    return readFile_.read(qMin(maxBytes, received_ - offset));
}

void StreamDownload::relocate(const QString& filePath)
{
    readFile_.close();
    filePath_ = filePath;
    file_.setFileName(filePath);
}

} // namespace Playback
