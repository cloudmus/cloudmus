#pragma once

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QTimer;

namespace Playback {

class NetworkPool;

// Where a stream is fetched from: its URL, the proxy to go through
// (QNetworkProxy::NoProxy: directly) and the headers the source asks for.
struct StreamSource {
    QUrl url;
    QNetworkProxy proxy;
    QMap<QString, QString> headers;
};

// One track downloaded whole into a file, as fast as the link gives it —
// not at the pace it is played. The file is readable while it grows (see
// read()), which is how StreamRelay plays a track that is still coming.
//
// Fetched in ranges of a few megabytes, one right after another: some
// services (YouTube) slow a single long request down to playback speed. A
// request that breaks off is picked up from the byte it stopped at.
class StreamDownload : public QObject {
    Q_OBJECT

public:
    StreamDownload(NetworkPool& network, const QString& filePath, QObject* parent = nullptr);
    ~StreamDownload() override;

    // Also for a fresh URL once the old one expired: the download goes on
    // from the byte it has got to.
    void setSource(const StreamSource& source) { source_ = source; }
    const StreamSource& source() const { return source_; }

    // Starts, or goes on after pause() or a failure.
    void start();
    // Drops the connection, keeps what's downloaded.
    void pause();
    // No further attempt will be made: whoever waits for more bytes
    // (StreamRelay) stops waiting.
    void giveUp();

    bool isActive() const { return reply_ != nullptr || retryPending_; }
    bool isComplete() const { return complete_; }
    bool hasFailed() const { return failed_; }
    bool isAbandoned() const { return abandoned_; }
    // The server has answered: the type is known, and the length if it
    // said one.
    bool hasMeta() const { return hasMeta_; }
    qint64 bytesAvailable() const { return received_; }
    qint64 totalLength() const { return total_; } // -1: not known (yet)
    QByteArray contentType() const { return contentType_; }

    // Up to `maxBytes` of what's downloaded, from `offset`.
    QByteArray read(qint64 offset, qint64 maxBytes);

    QString filePath() const { return filePath_; }
    // The finished file was moved (into the cache).
    void relocate(const QString& filePath);

signals:
    void metaReady();
    void progressed();
    void completed();
    // `expired`: the URL is no longer accepted (403/410) — a freshly
    // resolved one may be.
    void failed(bool expired);
    void abandoned();

private:
    void request();
    void onMetaData();
    void onData();
    void onFinished();
    void retryOrFail();
    void finish();
    void fail(bool expired);
    void dropReply();

    NetworkPool& network_;
    StreamSource source_;
    QString filePath_;
    QFile file_;
    QFile readFile_;
    QPointer<QNetworkReply> reply_;
    QTimer* retryTimer_ = nullptr;
    bool retryPending_ = false;

    QByteArray contentType_;
    qint64 received_ = 0;
    qint64 total_ = -1;
    bool hasMeta_ = false;
    bool complete_ = false;
    bool failed_ = false;
    bool abandoned_ = false;
    // The server ignores Range: the one request carries the whole body.
    bool wholeBody_ = false;

    // About the request under way.
    int httpStatus_ = 0;
    bool statusSeen_ = false;
    bool gotData_ = false;
    int attempts_ = 0;
};

} // namespace Playback
