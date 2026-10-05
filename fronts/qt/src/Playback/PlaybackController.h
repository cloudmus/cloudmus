#pragma once

#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

#include "Coro.h"
#include "LevelFeed.h"
#include "Models.h"
#include "PlayMode.h"
#include "RpcClient.h"
#include "SourceManager.h"

class QTimer;

namespace Playback {

class AudioPlayer;

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
class PlaybackController : public QObject {
    Q_OBJECT

public:
    explicit PlaybackController(Rpc::SourceManager& sourceManager, QObject* parent = nullptr);
    ~PlaybackController() override;

    // How a source's stream URLs are fetched — see AudioPlayer::play()'s
    // `route` (none: as the system has it). Asked at each play, so a
    // changed connection applies from the next track.
    using StreamRouteProvider = std::function<std::optional<QNetworkProxy>(const QString& sourceId)>;
    void setStreamRouteProvider(StreamRouteProvider provider) { streamRouteProvider_ = std::move(provider); }

    void loadQueue(const QString& sourceId, const QList<Track>& tracks, int startIndex);
    // Same, for a queue whose entries can come from different sources
    // (e.g. History) — each QueueEntry carries its own sourceId.
    void loadQueue(const QVector<QueueEntry>& entries, int startIndex);
    // Jumps to `index` within the current queue without replacing it.
    void playAt(int index);
    void startRadio(const QString& sourceId, const QString& stationId, const QList<Track>& initialTracks);

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
    void bufferedChanged(qint64 bufferedMs); // -1: not cached (a local file)
    void seeked(qint64 positionMs);
    void loadingChanged(bool loading); // waiting on track/streamReady — drives the busy indicator
    void errorOccurred(QString message);
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
    void prepareNext();
    Rpc::Task<void> prepareNextAsync(int generation, int nextIndex, QueueEntry entry);
    void invalidatePrepared();
    void promotePrepared(int nextIndex, bool manual);
    QString titleFor(const Track& track) const;
    void advance(int delta, bool wasSkip);
    // The current track failed to load or start: resolved again a few
    // times, then left paused with Play loading it anew.
    void handleStartFailure(const QString& message);
    // The queue index `delta` steps from the current one in play order
    // (shuffled or not), wrapping around under RepeatMode::All; -1 past
    // either end otherwise.
    int stepFrom(int delta);
    // A new random play order: `first` (if a valid index) leads, the rest
    // follow in random order.
    void reshuffle(int first);
    void setWaveMode(bool on);
    void sendFeedbackFinishedOrSkip(bool wasSkip);

    Rpc::SourceManager& sourceManager_;
    AudioPlayer* audioPlayer_ = nullptr;
    StreamRouteProvider streamRouteProvider_;

    QVector<QueueEntry> queue_;
    int index_ = -1;
    bool playing_ = false;

    int latestRequestId_ = -1;
    int transitionGeneration_ = 0;
    std::optional<StreamReadyParams> earlyStreamReady_;
    int preparedGeneration_ = 0;
    int preparedRequestId_ = -1;
    QPointer<Rpc::RpcClient> preparedClient_;
    int preparedIndex_ = -1;
    bool preparedReady_ = false;
    bool preparedStartPending_ = false;
    // Fresh resolutions tried for the current track since it last started.
    int startRetries_ = 0;
    // The current track failed for good: Play loads it again.
    bool reloadOnResume_ = false;
    std::optional<QNetworkProxy> preparedRoute_;
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
