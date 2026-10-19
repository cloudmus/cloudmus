#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace Net {

// A file downloaded in ranges over several connections at once: a server
// (GitHub's storage, YouTube) that holds one connection to a few hundred
// kilobytes a second gives a few of them as much each. All in this
// thread: the connections are QNetworkAccessManager requests, not threads.
//
// The first range tells the length; the rest is cut into ranges of
// rangeBytes(), and each free connection takes the earliest one left, so
// the file fills from the start (contiguousBytes()) and can be played
// while it comes. A range that breaks off is picked up from the byte it
// stopped at. A server that ignores Range gets one request for the whole
// body; one that doesn't say the length, ranges one after another.
class ParallelDownload : public QObject {
    Q_OBJECT

public:
    explicit ParallelDownload(const QString& filePath, QObject* parent = nullptr);
    ~ParallelDownload() override;

    // Also for a fresh URL once the old one expired: the download goes on
    // with what it has. `network` carries the proxy.
    void setSource(QNetworkAccessManager* network, const QUrl& url, const QMap<QString, QString>& headers = { });
    QUrl url() const { return url_; }

    // How many ranges are fetched at once; from the next request on.
    void setConnections(int connections);
    int connections() const { return connections_; }
    // Under the ~10 MB above which YouTube slows a single request down.
    static constexpr qint64 kDefaultRangeBytes = 8 * 1024 * 1024;
    void setRangeBytes(qint64 bytes) { rangeBytes_ = qMax<qint64>(bytes, 1); }
    qint64 rangeBytes() const { return rangeBytes_; }

    // Starts, or goes on after pause() or a failure.
    void start();
    // Drops the connections, keeps what's downloaded.
    void pause();
    // pause(), and lets go of the file and everything downloaded: the
    // next start() begins anew.
    void reset();

    bool isActive() const;
    // The requests under way.
    int activeConnections() const;
    bool isComplete() const { return complete_; }
    bool hasFailed() const { return failed_; }
    // The server has answered: the type is known, and the length if it
    // said one.
    bool hasMeta() const { return hasMeta_; }
    // The bytes from the start with no gap: what can be read.
    qint64 contiguousBytes() const { return contiguous_; }
    // All the bytes downloaded, gaps or not: the progress.
    qint64 receivedBytes() const { return received_; }
    qint64 totalLength() const { return total_; } // -1: not known (yet)
    QByteArray contentType() const { return contentType_; }

    // Why it failed: the HTTP status of an answer that wasn't the file (0
    // if it was the link or the disk), and what went wrong.
    int failedStatus() const { return failedStatus_; }
    QString errorString() const { return errorString_; }

    QString filePath() const { return file_.fileName(); }
    // The finished file was moved.
    void relocate(const QString& filePath);

signals:
    void metaReady();
    void progressed();
    void completed();
    // `expired`: the URL is no longer accepted (403/410) — a fresh one may
    // be (setSource()).
    void failed(bool expired);

private:
    // [start, end) of the file; end is -1 while the length isn't known.
    struct Range {
        qint64 start = 0;
        qint64 end = -1;
        qint64 done = 0;
        QPointer<QNetworkReply> reply;
        bool retryPending = false;
        int attempts = 0;
        // When the range was first asked for (ms on clock_), for the log.
        qint64 startedMs = -1;
        // About the request under way.
        int httpStatus = 0;
        bool statusSeen = false;
        bool gotData = false;

        qint64 next() const { return start + done; }
        bool isComplete() const { return end >= 0 && next() >= end; }
        bool isBusy() const { return reply != nullptr || retryPending; }
    };

    bool openFile();
    void pump();
    void request(Range& range);
    Range* rangeOf(QNetworkReply* reply);
    void onMetaData(QNetworkReply* reply);
    void onData(QNetworkReply* reply);
    void onFinished(QNetworkReply* reply);
    void lengthKnown(qint64 total);
    void takeWholeBody(QNetworkReply* reply);
    void retryOrFail(Range& range, const QString& error);
    void updateContiguous();
    void finish();
    void fail(bool expired, int status, const QString& error);
    void dropReplies();

    QNetworkAccessManager* network_ = nullptr;
    QUrl url_;
    QMap<QString, QString> headers_;
    int connections_ = 1;
    qint64 rangeBytes_ = kDefaultRangeBytes;

    QFile file_;
    // Sorted by start, end to end.
    QList<Range> ranges_;
    // Bumped whenever ranges_ or the connections are dropped: a retry timer
    // set before is stale.
    int generation_ = 0;
    // The server ignores Range: the one request carries the whole body.
    bool wholeBody_ = false;

    QByteArray contentType_;
    qint64 received_ = 0;
    qint64 contiguous_ = 0;
    qint64 total_ = -1;
    bool hasMeta_ = false;
    bool complete_ = false;
    bool failed_ = false;
    int failedStatus_ = 0;
    QString errorString_;

    QElapsedTimer clock_;
    qint64 receivedAtStart_ = 0;
};

} // namespace Net
