#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "ParallelDownload.h"
#include "Throttle.h"

class QNetworkAccessManager;

namespace Update {

// Downloads an update into a file over several connections at once
// (Net::ParallelDownload): GitHub's storage holds each one to a fraction
// of the line. A cancelled or failed download leaves no file behind.
class UpdateDownloader : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownloader(QNetworkAccessManager& network, QObject* parent = nullptr);
    ~UpdateDownloader() override;

    // `expectedSize` (0 if unknown) is checked once done: a cut-off
    // download must not get installed.
    void start(const QUrl& url, qint64 expectedSize, const QString& filePath);
    void cancel();
    bool isRunning() const { return download_ != nullptr; }

signals:
    // `secondsLeft` is -1 until there's a speed to estimate it from.
    void progress(qint64 received, qint64 total, int secondsLeft);
    void finished(const QString& filePath);
    void failed(const QString& error);

private:
    void handleProgress();
    void logProgress();
    void handleFinished();
    void handleFailure();
    void discard();

    struct Sample {
        qint64 ms = 0;
        qint64 bytes = 0;
    };

    QNetworkAccessManager& network_;
    Net::ParallelDownload* download_ = nullptr;
    qint64 expectedSize_ = 0;
    QElapsedTimer clock_;
    // The last few seconds' progress: the speed behind the estimate
    // follows the connection without jumping with every chunk.
    QList<Sample> samples_;
    int secondsLeft_ = -1;
    Timing::Throttle logThrottle_;
};

} // namespace Update
