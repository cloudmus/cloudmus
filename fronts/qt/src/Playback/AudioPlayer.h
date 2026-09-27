#pragma once

#include <QByteArray>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

struct mpv_handle;
struct mpv_event;

namespace Playback {

class StreamRelay;

// Thin wrapper around libmpv — see docs/adr or the AudioPlayer.cpp comment
// for why this replaced Qt Multimedia's QMediaPlayer: on hybrid Intel/AMD +
// NVIDIA laptops, QMediaPlayer's FFmpeg backend woke the discrete GPU and
// held it awake for the whole session even for plain audio (see git log).
// mpv is configured with vid=no (audio only, no video output/GPU context)
// and fed stream URLs directly, same as fronts/tui's playback_engine.py —
// no local buffering workaround needed here; the TLS-reset issue that
// forced that workaround for QMediaPlayer was specific to Qt's own FFmpeg
// HTTP client, not observed with mpv against the same CDN.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "pulse,alsa");
    ~AudioPlayer() override;

    // `route`: fetch the stream through this proxy (NoProxy: explicitly
    // directly), via StreamRelay; none — as the system has it, straight
    // from mpv.
    void play(const QString& url, const QString& title, const std::optional<QNetworkProxy>& route = std::nullopt,
        const QMap<QString, QString>& headers = { });
    void prepare(const QString& url, const QString& title, const std::optional<QNetworkProxy>& route,
        const QMap<QString, QString>& headers = { });
    void clearPrepared();
    void usePrepared(bool manual);
    void pause();
    void resume();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);

signals:
    // Playback has actually begun producing audio (mirrors
    // MPV_EVENT_PLAYBACK_RESTART).
    void started();
    // Playback failed; no started()/endOfFile() will follow for this file.
    void failed(QString message);
    // Natural end of the current track (not a manual stop/track change).
    void endOfFile();
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void prepared();

private:
    // Drains libmpv's event queue on the GUI thread. Invoked (via
    // Qt::QueuedConnection) from mpvWakeup(), which libmpv calls from one of
    // its own internal threads — never touch mpv_ directly from there.
    Q_INVOKABLE void processMpvEvents();
    void handleEvent(const mpv_event& event);

    static void mpvWakeup(void* ctx);

    void loadUrl(const QString& url);
    StreamRelay* relay();

    mpv_handle* mpv_ = nullptr;
    qint64 lastPositionMs_ = 0;
    qint64 lastDurationMs_ = 0;

    // Owned by this (parented), not mpv_ — see AudioPlayer.cpp's play()
    // for why a redirect-resolution preflight through Qt's own network
    // stack exists at all before handing mpv a URL.
    QNetworkAccessManager* networkManager_ = nullptr;
    // The in-flight preflight request, if any — a fresh play() call
    // aborts and replaces this rather than letting a stale reply from an
    // already-superseded track race the new one.
    QNetworkReply* pendingRedirectResolve_ = nullptr;
    // Created on first use: only a source with its own connection needs it.
    StreamRelay* relay_ = nullptr;
    QString preparedTitle_;
    bool preparedQueued_ = false;
    bool preparedPromoting_ = false;
};

} // namespace Playback
