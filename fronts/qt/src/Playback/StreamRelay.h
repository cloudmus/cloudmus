#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

namespace Playback {

// A local HTTP endpoint mpv plays from, fetching the real stream through a
// chosen QNetworkProxy (HTTP or SOCKS5 — mpv's bundled ffmpeg can't do
// SOCKS at all, and has no per-file proxy setting in the AppImage's mpv),
// or explicitly without one. What a source's stream URL needs when the
// source goes through a proxy: the service may only honor the URL from the
// address that asked for it.
//
// Each mpv connection is relayed as is — GET or HEAD, its Range passed on,
// the status, length and range headers passed back — so seeking and mpv's
// own reconnects work like against the server itself. On top of that a
// relayed body that breaks off mid-way (network error, reset, a stall
// past kStallTimeoutMs) is picked up again from the byte it stopped at,
// with a Range request, up to kMaxResumeAttempts times: mpv only sees a
// pause its cache rides out. Whatever can't be resumed (the server ignores
// Range, the URL expired with a 403/410) ends the connection early, and
// mpv's own reconnect or error handling takes it from there.
class StreamRelay : public QObject {
    Q_OBJECT

public:
    explicit StreamRelay(QObject* parent = nullptr);

    // The local URL to hand mpv for `upstream`, fetched through `proxy`
    // (QNetworkProxy::NoProxy: directly). The last few URLs stay valid, so
    // a seek or reconnect into a track that's still playing keeps working.
    QUrl urlFor(const QUrl& upstream, const QNetworkProxy& proxy, const QMap<QString, QString>& headers = { });
    // Fetch the beginning of one upcoming stream. A ready URL serves those
    // bytes locally before continuing with an upstream Range request.
    QUrl prefetch(const QUrl& upstream, const QNetworkProxy& proxy, const QMap<QString, QString>& headers = { });
    void cancelPrefetch();

signals:
    void prefetchReady(const QUrl& localUrl);

private:
    friend class RelayConnection;

    struct Target {
        QUrl upstream;
        QNetworkProxy proxy;
        QMap<QString, QString> headers;
        QByteArray prefix;
        QByteArray contentType;
        qint64 totalLength = -1;
        bool complete = false;
        bool ready = false;
    };

    void onNewConnection();
    const Target* target(const QByteArray& token) const;
    // One per distinct proxy: QNetworkAccessManager's proxy is per manager,
    // and keeping it keeps its connections pooled across seeks.
    QNetworkAccessManager* managerFor(const QNetworkProxy& proxy);

    QTcpServer* server_ = nullptr;
    QHash<QByteArray, Target> targets_;
    QList<QByteArray> tokenOrder_; // oldest first
    QHash<QString, QNetworkAccessManager*> managers_;
    QNetworkReply* prefetchReply_ = nullptr;
    QByteArray prefetchToken_;
};

} // namespace Playback
