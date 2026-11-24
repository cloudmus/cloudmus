#pragma once

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QUrl>

#include "ParallelDownload.h"

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
// Fetched in ranges of a few megabytes (Net::ParallelDownload), over as
// many connections at once as setConnections() allows — one by default:
// some services (YouTube) slow a single long request down to playback
// speed. Only the bytes from the start with no gap count as available.
class StreamDownload : public QObject {
    Q_OBJECT

public:
    StreamDownload(NetworkPool& network, const QString& filePath, QObject* parent = nullptr);
    ~StreamDownload() override;

    // Also for a fresh URL once the old one expired: the download goes on
    // from the byte it has got to.
    void setSource(const StreamSource& source) { source_ = source; }
    const StreamSource& source() const { return source_; }
    // How many ranges are fetched at once.
    void setConnections(int connections) { download_.setConnections(connections); }
    int connections() const { return download_.connections(); }

    // Starts, or goes on after pause() or a failure.
    void start();
    // Drops the connections, keeps what's downloaded.
    void pause();
    // No further attempt will be made: whoever waits for more bytes
    // (StreamRelay) stops waiting.
    void giveUp();

    bool isActive() const { return download_.isActive(); }
    bool isComplete() const { return download_.isComplete(); }
    bool hasFailed() const { return download_.hasFailed(); }
    bool isAbandoned() const { return abandoned_; }
    // The server has answered: the type is known, and the length if it
    // said one.
    bool hasMeta() const { return download_.hasMeta(); }
    // From the start, with no gap: what read() gives.
    qint64 bytesAvailable() const { return download_.contiguousBytes(); }
    // All that's downloaded, gaps or not: the progress.
    qint64 bytesReceived() const { return download_.receivedBytes(); }
    qint64 totalLength() const { return download_.totalLength(); } // -1: not known (yet)
    QByteArray contentType() const { return download_.contentType(); }

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
    NetworkPool& network_;
    StreamSource source_;
    QString filePath_;
    Net::ParallelDownload download_;
    QFile readFile_;
    bool abandoned_ = false;
};

} // namespace Playback
