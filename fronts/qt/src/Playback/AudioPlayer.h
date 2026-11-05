#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

#include "LevelFeed.h"

class QTimer;

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
    // What a failed() was down to.
    enum class Failure {
        Stream, // the track didn't start: a stream refused or broken, say
        File, // what was given to play couldn't be opened or made sense of
        Output, // no audio output could be opened: no fault of the track's
    };
    Q_ENUM(Failure)

#ifdef Q_OS_WIN
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "wasapi");
#elif defined(Q_OS_MACOS)
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "coreaudio");
#elif defined(Q_OS_ANDROID)
    // The prebuilt libmpv's outputs: AudioTrack, and OpenSL ES as the other.
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "audiotrack,opensles");
#else
    explicit AudioPlayer(QObject* parent = nullptr, const QByteArray& audioOutput = "pipewire,pulse,alsa");
#endif
    ~AudioPlayer() override;

    // A file on disk, or a stream fetched as mpv reads it. `route`: fetch
    // the stream through this proxy (NoProxy: explicitly directly); none —
    // as the system has it.
    // Where the next file to be played starts, instead of its beginning.
    void setStartPosition(qint64 positionMs);
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

    // Soft edges: a pause, a stop, a seek or a track changed by hand first
    // takes the sound down over `fadeMs` and waits `settleMs` more for that
    // silence to reach the speakers, and only then does it — mpv cuts the
    // wave wherever it is, and the output's flush that follows (AudioTrack's
    // and OpenSL's alike) lets that be heard as a click. What starts after
    // comes up over the same `fadeMs`. 0: cut at once, as before. The fade is
    // mpv's volume gain, applied a whole output chunk at a time, so it's only
    // smooth with small chunks (setOutput()).
    void setEdges(int fadeMs, int settleMs);
    // mpv's audio output (its `ao` list) and, for OpenSL ES, how many frames
    // it hands over at a time (0: mpv's default, the whole 250 ms buffer).
    // Reopens the output.
    void setOutput(const QByteArray& output, int openslesFramesPerEnqueue);

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
    // Without an audio output, though, started() comes first: mpv 2.5
    // reports the file restarted before it gives up for want of one.
    void failed(QString message, Playback::AudioPlayer::Failure failure);
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
    bool startPositionSet_ = false;

    // mpv commands and property changes go through these: while the sound
    // fades out they queue up, in order, until it's silent (see setEdges()).
    void command(QList<QByteArray> args);
    void setProperty(const QByteArray& name, const QByteArray& value);
    void setPaused(bool paused);
    // Before something that cuts the sound: fade out first if it's heard.
    void fadeOutFirst();
    // Before a load or a seek with soft edges: what starts comes up from
    // silence once mpv says it restarted.
    void startSilent();
    void fadeTo(double level);
    void onFadeTick();
    void runQueued();
    void applyLevel(double level);
    bool heard() const;

    int fadeMs_ = 0;
    int settleMs_ = 0;
    QTimer* fadeTimer_ = nullptr;
    QTimer* settleTimer_ = nullptr;
    QElapsedTimer fadeClock_;
    double level_ = 1.0; // 0..1, the fade's own, on top of the volume
    double fadeFrom_ = 1.0;
    double fadeTarget_ = 1.0;
    int fadeSpanMs_ = 0;
    bool deferring_ = false;
    QList<std::function<void()>> queued_;
    // Logical state, as asked for — mpv's own lags while commands queue.
    bool paused_ = false;
    bool stopped_ = true;
    // A load or seek went out: the sound comes up on PLAYBACK_RESTART.
    bool awaitingRestart_ = false;
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
