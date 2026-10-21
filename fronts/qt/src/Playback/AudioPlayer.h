#pragma once

#include <QByteArray>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

#include "LevelFeed.h"

struct mpv_handle;
struct mpv_event;

namespace Playback {

class StreamDownload;
class StreamRelay;

// Thin wrapper around libmpv — see docs/adr or the AudioPlayer.cpp comment
// for why this replaced Qt Multimedia's QMediaPlayer: on hybrid Intel/AMD +
// NVIDIA laptops, QMediaPlayer's FFmpeg backend woke the discrete GPU and
// held it awake for the whole session even for plain audio (see git log).
// mpv is configured with vid=no (audio only, no video output/GPU context).
// It plays files from disk, and anything remote through StreamRelay.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
#ifdef Q_OS_WIN
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "wasapi");
#elif defined(Q_OS_MACOS)
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "coreaudio");
#else
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "pipewire,pulse,alsa");
#endif
    ~AudioPlayer() override;

    // A file on disk, or a stream fetched as mpv reads it. `route`: fetch
    // the stream through this proxy (NoProxy: explicitly directly); none —
    // as the system has it.
    void play(const QString& url, const QString& title, const std::optional<QNetworkProxy>& route = std::nullopt,
        const QMap<QString, QString>& headers = { });
    // A track `download` is fetching, played while it still comes.
    void play(StreamDownload* download, const QString& title);
    // Queues a file on disk behind what plays, to start without a gap when
    // that ends (see usePrepared()).
    void prepare(const QString& file, const QString& title);
    void clearPrepared();
    void usePrepared(bool manual);
    void pause();
    void resume();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);

    // Loudness readings of what plays (before the volume control), for
    // views that move with the music. Off by default: they cost a stream of
    // log lines from mpv while on.
    void setLevelsEnabled(bool enabled);
    // The readings the speakers have got to since the last call, oldest
    // first; none while off.
    QVector<LevelReading> takeLevels();

signals:
    // Playback has actually begun producing audio (mirrors
    // MPV_EVENT_PLAYBACK_RESTART).
    void started();
    // Playback failed; no started()/endOfFile() will follow for this file.
    // unplayableFile: what was given to play couldn't be opened or made
    // sense of — as opposed to, say, the audio output going away.
    void failed(QString message, bool unplayableFile);
    // Natural end of the current track (not a manual stop/track change).
    void endOfFile();
    void positionChanged(qint64 positionMs, qint64 durationMs);
    // How far into the track the audio is already downloaded; -1 when
    // mpv doesn't cache this stream (a local file).
    void bufferedChanged(qint64 bufferedMs);
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
    qint64 lastBufferedMs_ = -1;

    // Created on first use: a local file needs none.
    StreamRelay* relay_ = nullptr;
    QString preparedTitle_;
    bool preparedQueued_ = false;
    bool preparedPromoting_ = false;
    LevelFeed levelFeed_;
    bool levelsEnabled_ = false;
};

} // namespace Playback
