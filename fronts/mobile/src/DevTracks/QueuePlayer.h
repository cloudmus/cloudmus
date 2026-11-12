#pragma once

#include <QList>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include "Track.h"

namespace Playback {

class AudioPlayer;

// The queue and what plays from it — the MVP's stand-in for the desktop
// Playback::PlaybackController (which needs the backends): a fixed list of
// tracks, next/previous, the next track when one ends, and the last track
// and position kept across runs. The audio itself is the desktop's own
// Playback::AudioPlayer (libmpv), shared as is.
class QueuePlayer : public QObject {
    Q_OBJECT

public:
    QueuePlayer(QList<Track> tracks, QSettings& state, QObject* parent = nullptr);
    ~QueuePlayer() override;

    const QList<Track>& tracks() const { return tracks_; }
    // -1 with no tracks.
    int currentIndex() const { return index_; }
    const Track* currentTrack() const;
    bool playing() const { return playing_; }
    // Asked to play and no audio yet.
    bool loading() const { return loading_; }
    qint64 positionMs() const { return positionMs_; }
    qint64 durationMs() const;

    void play();
    void pause();
    void togglePause();
    void next();
    // Back to the start of the track once it has played a few seconds, to
    // the one before otherwise (as everywhere).
    void previous();
    void playIndex(int index);
    void seek(qint64 positionMs);

signals:
    void currentChanged();
    void playingChanged();
    void loadingChanged();
    void positionChanged();
    // The first audio of this run: startup timing.
    void firstAudio();

private:
    void start(int index, qint64 fromMs);
    void setPlaying(bool playing);
    void setLoading(bool loading);
    void saveState();

    QList<Track> tracks_;
    QSettings& state_;
    AudioPlayer* audio_ = nullptr;
    int index_ = -1;
    // The current file is open in mpv. Not right after a restart: the saved
    // track waits for play(), so the audio output isn't opened before
    // anything is to be heard.
    bool loaded_ = false;
    bool playing_ = false;
    bool loading_ = false;
    bool heardAudio_ = false;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    // Where the file should start once it's loaded.
    qint64 pendingSeekMs_ = 0;
    QTimer saveTimer_;
};

} // namespace Playback
