#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include "StreamDownload.h"

class QTcpServer;

namespace Playback {

class NetworkPool;

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
    // The local URL for a track that `download` is fetching: mpv gets the
    // bytes from the download's file as they arrive, so the track is
    // fetched once, at full speed, however mpv paces its reading.
    QUrl urlFor(StreamDownload* download);

private:
    friend class RelayConnection;

    struct Target {
        StreamSource source;
        QPointer<StreamDownload> download;
        bool followsDownload = false;
    };

    QUrl addTarget(const Target& target);
    void onNewConnection();
    const Target* target(const QByteArray& token) const;

    QTcpServer* server_ = nullptr;
    QHash<QByteArray, Target> targets_;
    QList<QByteArray> tokenOrder_; // oldest first
    NetworkPool* network_ = nullptr;
};

} // namespace Playback
