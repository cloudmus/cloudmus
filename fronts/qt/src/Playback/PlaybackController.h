#pragma once

#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

#include "Coro.h"
#include "LevelFeed.h"
#include "Models.h"
#include "PlayMode.h"
#include "RpcClient.h"
#include "SourceManager.h"

class QTemporaryDir;
class QTimer;

namespace Playback {

class AudioPlayer;
class Preloader;
class TrackCache;

struct QueueEntry {
    QString sourceId;
    Track track;
    // Put there by the user (Play Next / Add to Queue) — survives a radio
    // replacing its upcoming tracks (see handleTracksAdded()).
    bool userQueued = false;
};

// Ties an Rpc::SourceManager (many backends) to one shared AudioPlayer
// (Qt Multimedia): only used for providesStream backends (both shipped
// backends are), owns the queue, and resolves playback.play's async
// track/streamReady via the requestId correlation rule in
// docs/protocol.md §11.1 — a "next" pressed twice discards the stream that
// arrives for the now-superseded play.
//
// Tracks are downloaded whole rather than streamed at the pace they play:
// the playing one first, then the next few of the queue, one at a time (see
// Preloader). What's downloaded plays from the TrackCache, without asking
// the source again.
class PlaybackController : public QObject {
    Q_OBJECT

public:
    // `trackCacheDir`: where downloaded tracks are kept from run to run;
    // empty — in a directory of their own that goes with this object.
    explicit PlaybackController(
        Rpc::SourceManager& sourceManager, QObject* parent = nullptr, const QString& trackCacheDir = QString());
    ~PlaybackController() override;

    Preloader& preloader() { return *preloader_; }
    TrackCache& trackCache() { return *trackCache_; }
    // How many tracks past the playing one are downloaded ahead: of a
    // playlist, and of a radio (whose upcoming tracks a skip replaces).
    // 0: only the playing track.
    void setPreloadDepth(int playlistTracks, int radioTracks);
    int playlistPreloadDepth() const { return playlistPreloadDepth_; }
    // Longest a radio's queue (and the list showing it) may get.
    static constexpr int kMaxRadioQueue = 100;
    int radioPreloadDepth() const { return radioPreloadDepth_; }

    // How a source's stream URLs are fetched — see AudioPlayer::play()'s
    // `route` (none: as the system has it). Asked at each play, so a
    // changed connection applies from the next track.
    using StreamRouteProvider = std::function<std::optional<QNetworkProxy>(const QString& sourceId)>;
    void setStreamRouteProvider(StreamRouteProvider provider);

    void loadQueue(const QString& sourceId, const QList<Track>& tracks, int startIndex);
    // Same, for a queue whose entries can come from different sources
    // (e.g. History) — each QueueEntry carries its own sourceId.
    void loadQueue(const QVector<QueueEntry>& entries, int startIndex);
    // Jumps to `index` within the current queue without replacing it.
    void playAt(int index);
    // An empty `stationId`: the station's tracks as they were last run,
    // played before the source is there to run it — see attachStation().
    void startRadio(
        const QString& sourceId, const QString& stationId, const QList<Track>& initialTracks, int startIndex = 0);
    // A radio playing without a station behind it yet.
    bool awaitsStation() const { return waveMode_ && waveStationId_.isEmpty(); }
    // Puts a started station behind the radio that awaitsStation(): what
    // plays goes on playing, and `tracks` replace the old upcoming ones
    // like a radio/tracksAdded with replaceUpcoming.
    void attachStation(const QString& sourceId, const QString& stationId, const QList<Track>& tracks);
    // The source answers now: what couldn't be preloaded while it was
    // still starting gets another try.
    void sourceCameUp(const QString& sourceId);

    // Insert a single track without disturbing the rest of the queue
    // (unlike loadQueue/startRadio, which replace it wholesale) — for the
    // track list's context menu. Each QueueEntry already carries its own
    // sourceId, so a track from a different source than what's currently
    // playing queues just fine. If nothing is playing when either is
    // called, the queue was effectively empty for the user's purposes, so
    // "queue it" just means "play it now" — same fallback loadQueue/
    // startRadio already have via playIndex().
    void enqueueNext(const QString& sourceId, const Track& track);
    void enqueueAtEnd(const QString& sourceId, const Track& track);

    // Called by whoever wires up RpcClient::notifications for each source
    // (see Ui/MainWindow.cpp) — not signals themselves, since the caller
    // already has the per-client dispatch table to fill in.
    void handleStreamReady(const QString& sourceId, const StreamReadyParams& params);
    void handleTracksAdded(const QString& sourceId, const TracksAddedParams& params);

    void togglePause();
    void stop();
    // Stops like stop(), but not by choice: the current track's source went
    // away. No halted() — playback was still wanted.
    void dropCurrentTrack();
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);

    // Play modes, kept as the user set them; a radio overrides what it
    // can't do (its queue grows as it plays, so there's no whole list to
    // shuffle or start over) — see shuffleActive()/effectiveRepeatMode().
    // Shuffle picks a random play order over the queue without reordering
    // it, so the track list keeps its order.
    void setShuffle(bool on);
    void setRepeatMode(RepeatMode mode);
    bool shuffle() const { return shuffle_; }
    RepeatMode repeatMode() const { return repeatMode_; }
    bool isRadio() const { return waveMode_; }
    bool shuffleActive() const { return shuffle_ && !waveMode_; }
    RepeatMode effectiveRepeatMode() const
    {
        return waveMode_ && repeatMode_ == RepeatMode::All ? RepeatMode::Off : repeatMode_;
    }

    bool isPlaying() const { return playing_; }
    // See AudioPlayer::setLevelsEnabled()/takeLevels(); none while paused.
    void setLevelsEnabled(bool enabled);
    QVector<LevelReading> takeLevels();
    qint64 positionMs() const;
    bool hasCurrentTrack() const { return index_ >= 0 && index_ < queue_.size(); }
    bool hasQueue() const { return !queue_.isEmpty(); }
    const Track& currentTrack() const { return queue_[index_].track; }
    const QString& currentSourceId() const { return queue_[index_].sourceId; }
    const QVector<QueueEntry>& queue() const { return queue_; }
    int currentIndex() const { return index_; }

signals:
    void trackChanged(const Track& track, const QString& sourceId);
    void playingChanged(bool playing);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    // How far into the track it is downloaded — all of it for one that
    // plays from the cache; -1: nothing to show (a source's own local file).
    void bufferedChanged(qint64 bufferedMs);
    void seeked(qint64 positionMs);
    void loadingChanged(bool loading); // waiting on track/streamReady — drives the busy indicator
    void errorOccurred(QString message);
    // The same failure, for statistics: the source it was on and a fixed
    // reason — "unavailable" (the source is down), "rejected" (the source
    // refused to play the track), "stream" (the audio didn't start),
    // "timeout", "station_empty".
    void failed(const QString& sourceId, const QString& reason);
    // Emitted whenever hasCurrentTrack() actually changes: true right
    // before trackChanged() when a play succeeds, false when stop()
    // clears the current track back to undefined. The single declarative
    // source of truth UI enablement (play/pause, stop, seek) and
    // MainWindow's hero-panel/row-highlight reset react to — see
    // NowPlayingBar::setTrackAvailable().
    void currentTrackAvailabilityChanged(bool available);
    // Emitted whenever hasQueue() changes (loadQueue()/startRadio()
    // populate it) — declaratively drives previous/next enablement.
    void queueAvailabilityChanged(bool available);
    // Emitted whenever queue() changes content — replaced, inserted into,
    // or extended by a radio's tracksAdded. The main track list mirrors it.
    void queueChanged();
    // Playback was paused or stopped on purpose (by the user, or at the
    // end of the queue) — not a track change, a failure or a lost source.
    void halted();
    // Shuffle/repeat changed, or a radio started or ended (which changes
    // what's available — see isRadio()).
    void playModeChanged();

private:
    void playIndex(int index, bool isRetry = false);
    // isRetry: re-resolving the track that just failed to start — it was
    // already announced (trackChanged), so history and notifications must
    // not see it again.
    Rpc::Task<void> playIndexAsync(int index, bool isRetry);
    void stopForTransition();
    // `index` is what plays from now on: bookkeeping and announcements.
    void becomeCurrent(int index, const QueueEntry& entry, bool isRetry);
    // Hands the current track to mpv: its file if it's all there, else its
    // download as it comes, else the stream itself.
    void startCurrentAudio(const std::optional<StreamDescriptor>& stream);
    // Tells the Preloader what's wanted now: the playing (or starting)
    // track and the ones after it.
    void updatePreloadWindow();
    // Works out how much of the playing track to show as downloaded, and
    // says so if that changed (bufferedChanged()). `force`: say it anyway —
    // for a new track, whose bar starts over.
    void updateBuffered(bool force = false);
    // Queues the next track in mpv once it's all downloaded, so it starts
    // without a gap.
    void prepareNext();
    void invalidatePrepared();
    // A radio's queue only grows; drops its oldest played tracks past
    // kMaxRadioQueue (never the current one, nor anything after it).
    void trimRadioQueue();
    Rpc::Task<StreamDescriptor> resolveStreamAsync(QPointer<Rpc::RpcClient> client, int requestId, QString trackId);
    void promotePrepared(int nextIndex, bool manual);
    QString titleFor(const Track& track) const;
    void advance(int delta, bool wasSkip);
    // The current track failed to load or start: resolved again a few
    // times, then left paused with Play loading it anew.
    void handleStartFailure(const QString& message, const QString& reason);
    void reportError(const QString& sourceId, const QString& reason, const QString& message);
    // The queue index `delta` steps from the current one in play order
    // (shuffled or not), wrapping around under RepeatMode::All; -1 past
    // either end otherwise.
    int stepFrom(int delta);
    // What follows `from` in play order without changing anything; -1 at
    // the end — also where a shuffled list would start another round, whose
    // order isn't known yet.
    int peekNext(int from) const;
    // A new random play order: `first` (if a valid index) leads, the rest
    // follow in random order.
    void reshuffle(int first);
    void setWaveMode(bool on);
    void sendFeedbackFinishedOrSkip(bool wasSkip);

    Rpc::SourceManager& sourceManager_;
    AudioPlayer* audioPlayer_ = nullptr;
    std::unique_ptr<QTemporaryDir> ownCacheDir_;
    TrackCache* trackCache_ = nullptr;
    Preloader* preloader_ = nullptr;
    int playlistPreloadDepth_ = 3;
    int radioPreloadDepth_ = 2;
    StreamRouteProvider streamRouteProvider_;

    QVector<QueueEntry> queue_;
    int index_ = -1;
    bool playing_ = false;

    int latestRequestId_ = -1;
    int transitionGeneration_ = 0;
    std::optional<StreamReadyParams> earlyStreamReady_;
    // The playing track's stream as playback.play resolved it; none when
    // it plays from the cache.
    std::optional<StreamDescriptor> currentStream_;
    int preparedIndex_ = -1;
    bool preparedReady_ = false;
    bool preparedStartPending_ = false;
    // Fresh resolutions tried for the current track since it last started.
    int startRetries_ = 0;
    // The current track failed for good: Play loads it again.
    bool reloadOnResume_ = false;
    // Queue index of the in-flight playback.play (-1 if none): it's about
    // to become index_, so a radio replacing its upcoming tracks must keep it.
    int startingIndex_ = -1;
    // A radio ran out of queued tracks at the end of one: the next
    // radio/tracksAdded continues playback (see advance()/handleTracksAdded()).
    bool awaitingRadioTracks_ = false;
    int radioWaitGeneration_ = 0;
    QString latestRequestSourceId_;
    QTimer* playTimeoutTimer_ = nullptr;
    qint64 lastKnownPositionMs_ = 0;
    qint64 lastKnownDurationMs_ = 0;
    // What mpv has read ahead: all there is to show for a stream it
    // fetches itself (no download of ours).
    qint64 mpvBufferedMs_ = -1;
    qint64 shownBufferedMs_ = -1;

    bool shuffle_ = false;
    RepeatMode repeatMode_ = RepeatMode::Off;
    // Queue indices in shuffled play order — maintained only while
    // shuffleActive().
    QVector<int> order_;

    bool waveMode_ = false;
    QString waveSourceId_;
    QString waveStationId_;
};

} // namespace Playback
