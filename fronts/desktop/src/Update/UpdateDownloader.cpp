#include "UpdateDownloader.h"

#include <QFile>
#include <QLoggingCategory>

#include <cmath>

namespace Update {

namespace {
constexpr qint64 kSpeedWindowMs = 3000;
// Too short a stretch to tell a speed from.
constexpr qint64 kMinSpeedSpanMs = 500;
// Measured on a release AppImage straight from GitHub: one connection
// 1.1 MiB/s, five 3.1 MiB/s, ten hardly more.
constexpr int kConnections = 5;
// Qt's network access manager opens no more than six to a host.
constexpr int kMaxConnections = 6;
// How often the debug log tells how the download is going.
constexpr int kLogIntervalMs = 5000;

Q_LOGGING_CATEGORY(lcUpdate, "cloudmus.update")

// kConnections, or what CLOUDMUS_UPDATE_CONNECTIONS says — to measure
// another number on a real update.
int connections()
{
    bool ok = false;
    const int wanted = qEnvironmentVariableIntValue("CLOUDMUS_UPDATE_CONNECTIONS", &ok);
    return ok && wanted > 0 ? qMin(wanted, kMaxConnections) : kConnections;
}

double mibPerSecond(qint64 bytes, qint64 ms) { return double(bytes) / 1048.576 / double(qMax<qint64>(ms, 1)); }
} // namespace

UpdateDownloader::UpdateDownloader(QNetworkAccessManager& network, QObject* parent)
    : QObject(parent)
    , network_(network)
    , logThrottle_(kLogIntervalMs, [this]() { logProgress(); })
{
}

UpdateDownloader::~UpdateDownloader() { cancel(); }

void UpdateDownloader::start(const QUrl& url, qint64 expectedSize, const QString& filePath)
{
    cancel();
    expectedSize_ = expectedSize;
    samples_.clear();
    secondsLeft_ = -1;
    qCDebug(lcUpdate).noquote() << "downloading" << url.toString() << "(" << expectedSize << "bytes expected) into"
                                << filePath << "over" << connections() << "connections";
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // Queued, like every other outcome: the caller may not be
        // listening yet.
        qCWarning(lcUpdate).noquote() << "can't write" << filePath << ":" << file.errorString();
        QMetaObject::invokeMethod(
            this, [this, error = file.errorString()]() { emit failed(error); }, Qt::QueuedConnection);
        return;
    }
    file.close();
    download_ = new Net::ParallelDownload(filePath, this);
    download_->setSource(&network_, url);
    download_->setConnections(connections());
    connect(download_, &Net::ParallelDownload::progressed, this, &UpdateDownloader::handleProgress);
    connect(download_, &Net::ParallelDownload::completed, this, &UpdateDownloader::handleFinished);
    connect(download_, &Net::ParallelDownload::failed, this, &UpdateDownloader::handleFailure);
    clock_.start();
    download_->start();
}

void UpdateDownloader::cancel()
{
    if (download_ == nullptr)
        return;
    qCDebug(lcUpdate).noquote() << "download cancelled at" << download_->receivedBytes() << "bytes after"
                                << clock_.elapsed() << "ms";
    logThrottle_.cancel();
    discard();
}

void UpdateDownloader::handleProgress()
{
    const qint64 received = download_->receivedBytes();
    const qint64 total = download_->totalLength() > 0 ? download_->totalLength() : expectedSize_;
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
    secondsLeft_ = secondsLeft;
    logThrottle_.trigger();
    emit progress(received, total, secondsLeft);
}

void UpdateDownloader::logProgress()
{
    if (download_ == nullptr)
        return;
    const qint64 received = download_->receivedBytes();
    const qint64 ms = clock_.elapsed();
    qCDebug(lcUpdate).nospace().noquote()
        << "downloaded " << received << " of " << download_->totalLength() << " bytes in " << ms
        << " ms: " << mibPerSecond(received, ms) << " MiB/s on average, " << download_->activeConnections()
        << " connections open, " << (secondsLeft_ >= 0 ? QString::number(secondsLeft_) : QStringLiteral("?"))
        << " s left";
}

void UpdateDownloader::handleFinished()
{
    logThrottle_.cancel();
    const QString filePath = download_->filePath();
    const qint64 size = QFile(filePath).size();
    const qint64 ms = clock_.elapsed();
    qCDebug(lcUpdate).nospace().noquote() << "downloaded " << size << " bytes in " << ms << " ms ("
                                          << mibPerSecond(size, ms) << " MiB/s) into " << filePath;
    if (expectedSize_ > 0 && size != expectedSize_) {
        qCWarning(lcUpdate).noquote() << "expected" << expectedSize_ << "bytes, got" << size;
        discard();
        emit failed(tr("Downloaded %1 bytes of %2").arg(size).arg(expectedSize_));
        return;
    }
    download_->deleteLater();
    download_ = nullptr;
    emit finished(filePath);
}

void UpdateDownloader::handleFailure()
{
    const int status = download_->failedStatus();
    const QString error = status > 0 && (status < 200 || status >= 300)
        ? tr("The server answered with HTTP %1").arg(status)
        : download_->errorString();
    logThrottle_.cancel();
    qCWarning(lcUpdate).noquote() << "download failed after" << clock_.elapsed() << "ms at"
                                  << download_->receivedBytes() << "bytes:" << error;
    discard();
    emit failed(error);
}

void UpdateDownloader::discard()
{
    const QString filePath = download_->filePath();
    download_->disconnect(this);
    download_->reset();
    download_->deleteLater();
    download_ = nullptr;
    QFile::remove(filePath);
}

} // namespace Update
