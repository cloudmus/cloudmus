#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace Update {

// Downloads an update into a file, all in one go (no resuming: updates
// are small). A cancelled or failed download leaves no file behind.
class UpdateDownloader : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownloader(QNetworkAccessManager& network, QObject* parent = nullptr);
    ~UpdateDownloader() override;

    // `expectedSize` (0 if unknown) is checked once done: a cut-off
    // download must not get installed.
    void start(const QUrl& url, qint64 expectedSize, const QString& filePath);
    void cancel();
    bool isRunning() const { return reply_ != nullptr; }

signals:
    // `secondsLeft` is -1 until there's a speed to estimate it from.
    void progress(qint64 received, qint64 total, int secondsLeft);
    void finished(const QString& filePath);
    void failed(const QString& error);

private:
    void writeAvailable();
    void handleProgress(qint64 received, qint64 total);
    void handleFinished();
    void fail(const QString& error);
    void discard();

    struct Sample {
        qint64 ms = 0;
        qint64 bytes = 0;
    };

    QNetworkAccessManager& network_;
    QPointer<QNetworkReply> reply_;
    QFile file_;
    qint64 expectedSize_ = 0;
    QElapsedTimer clock_;
    // The last few seconds' progress: the speed behind the estimate
    // follows the connection without jumping with every chunk.
    QList<Sample> samples_;
};

} // namespace Update
