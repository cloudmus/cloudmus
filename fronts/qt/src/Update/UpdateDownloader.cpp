#include "UpdateDownloader.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <cmath>

namespace Update {

namespace {
constexpr qint64 kSpeedWindowMs = 3000;
// Too short a stretch to tell a speed from.
constexpr qint64 kMinSpeedSpanMs = 500;
} // namespace

UpdateDownloader::UpdateDownloader(QNetworkAccessManager& network, QObject* parent)
    : QObject(parent)
    , network_(network)
{
}

UpdateDownloader::~UpdateDownloader() { cancel(); }

void UpdateDownloader::start(const QUrl& url, qint64 expectedSize, const QString& filePath)
{
    cancel();
    expectedSize_ = expectedSize;
    samples_.clear();
    file_.setFileName(filePath);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // Queued, like every other outcome: the caller may not be
        // listening yet.
        QMetaObject::invokeMethod(
            this, [this, error = file_.errorString()]() { emit failed(error); }, Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(url);
    // GitHub serves release assets through a redirect to its storage.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    clock_.start();
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::readyRead, this, &UpdateDownloader::writeAvailable);
    connect(reply_, &QNetworkReply::downloadProgress, this, &UpdateDownloader::handleProgress);
    connect(reply_, &QNetworkReply::finished, this, &UpdateDownloader::handleFinished);
}

void UpdateDownloader::cancel()
{
    if (!reply_)
        return;
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    discard();
}

void UpdateDownloader::writeAvailable()
{
    if (!reply_)
        return;
    const QByteArray data = reply_->readAll();
    if (file_.write(data) != data.size())
        fail(file_.errorString());
}

void UpdateDownloader::handleProgress(qint64 received, qint64 total)
{
    if (total <= 0)
        total = expectedSize_;
    const qint64 now = clock_.elapsed();
    samples_.append({ now, received });
    while (samples_.size() > 2 && now - samples_[1].ms >= kSpeedWindowMs)
        samples_.removeFirst();
    int secondsLeft = -1;
    const Sample& oldest = samples_.first();
    const qint64 spanMs = now - oldest.ms;
    if (total > 0 && spanMs >= kMinSpeedSpanMs && received > oldest.bytes) {
        const double bytesPerMs = double(received - oldest.bytes) / double(spanMs);
        secondsLeft = int(std::ceil(double(total - received) / bytesPerMs / 1000.0));
    }
    emit progress(received, total, secondsLeft);
}

void UpdateDownloader::handleFinished()
{
    if (!reply_)
        return;
    writeAvailable();
    if (!reply_)
        return; // the write failed
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        discard();
        emit failed(reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                             : tr("The server answered with HTTP %1").arg(status));
        return;
    }
    const qint64 size = file_.size();
    if (!file_.flush() || (expectedSize_ > 0 && size != expectedSize_)) {
        const QString error = file_.error() != QFileDevice::NoError
            ? file_.errorString()
            : tr("Downloaded %1 bytes of %2").arg(size).arg(expectedSize_);
        discard();
        emit failed(error);
        return;
    }
    file_.close();
    emit finished(file_.fileName());
}

void UpdateDownloader::fail(const QString& error)
{
    qWarning() << "Update download failed:" << error;
    cancel();
    emit failed(error);
}

void UpdateDownloader::discard()
{
    file_.close();
    file_.remove();
}

} // namespace Update
