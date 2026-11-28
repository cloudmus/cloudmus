#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

#include "AudioPlayer.h"
#include "Coro.h"
#include "LevelFeed.h"
#include "Models.h"
#include "PlayMode.h"
#include "RpcClient.h"
#include "SourceManager.h"

class QTemporaryDir;
class QTimer;

namespace Playback {

class Preloader;
class TrackCache;

struct QueueEntry {
    QString sourceId;
    Track track;
    // Put there by the user (Play Next / Add to Queue) — survives a radio
    // replacing its upcoming tracks (see handleTracksAdded()).
    bool userQueued = false;
    QString localInput = { };
    bool persistentInput = false;
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
    // How long a track may take to start before that counts as a failure;
    // for the starts after this call.
    void setStartTimeout(int ms);
    int playlistPreloadDepth() const { return playlistPreloadDepth_; }
    // Longest a radio's queue (and the list showing it) may get.
    static constexpr int kMaxRadioQueue = 100;
    int radioPreloadDepth() const { return radioPreloadDepth_; }

    // How a source's stream URLs are fetched — see AudioPlayer::play()'s
    // `route` (none: as the system has it). Asked at each play, so a
    // changed connection applies from the next track.
    using StreamRouteProvider = std::function<std::optional<QNetworkProxy>(const QString& sourceId)>;
    void setStreamRouteProvider(StreamRouteProvider provider);
    // How many connections the user wants a source's tracks downloaded
    // over; 0: as many as the stream suggests.
    using DownloadConnectionsProvider = std::function<int(const QString& sourceId)>;
    void setDownloadConnectionsProvider(DownloadConnectionsProvider provider);
    // Whether a radio's feedback for the source is to be kept rather than
    // sent (App::Outbox::holds()): then feedbackDeferred() takes it.
    using FeedbackHold = std::function<bool(const QString& sourceId)>;
    void setFeedbackHold(FeedbackHold hold) { feedbackHold_ = std::move(hold); }
    // Downloaded tracks of the source to go on with when a radio has nothing
    // more it can play without the network (ViewModel::ActivePlaylist):
    // up to `count`, none of those queued after the current one.
    using OfflineRadioProvider = std::function<QList<Track>(const QString& sourceId, int count)>;
    void setOfflineRadioProvider(OfflineRadioProvider provider) { offlineRadio_ = std::move(provider); }
    // Whether the source can be reached, asked before a track that isn't
    // downloaded is started from it (App::Connectivity::checkSource() for
    // one that was found offline, true at once otherwise).
    using ReachableCheck = std::function<Rpc::Task<bool>(QString sourceId)>;
    void setReachableCheck(ReachableCheck check) { reachable_ = std::move(check); }
    // How many tracks a radio is topped up with from the disk at a time:
    // few, so that a station back with the network soon has its say.
    static constexpr int kOfflineRadioBatch = 5;

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
    // The track, when it next starts, goes on from there rather than from its
    // beginning: what was playing when the app last ran. Not if little is left.
    void resumeAt(const QString& trackId, qint64 positionMs);
    void loadRadioQueue(const QString& sourceId, const QString& stationId, QVector<QueueEntry> entries, int startIndex);
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
    void playLocalInput(QueueEntry entry);
#ifdef Q_OS_ANDROID
    void setContentOpener(std::function<int(const QString&)> opener)
    {
        audioPlayer_->setContentOpener(std::move(opener));
    }
#endif
    void enqueueAtEnd(const QString& sourceId, const Track& track);

    // Called by whoever wires up RpcClient::notifications for each source
    // (see Ui/MainWindow.cpp) — not signals themselves, since the caller
    // already has the per-client dispatch table to fill in.
    void handleStreamReady(const QString& sourceId, const StreamReadyParams& params);
    void handleTracksAdded(const QString& sourceId, const TracksAddedParams& params);
    // A source's `error` whose data names the track and why it won't play
    // (docs/protocol.md §9.1). When it is the track being started, it is
    // told to the user and playback goes on to the next track that plays —
    // for "offline", the next downloaded one — at once, instead of waiting
    // out the start timeout. False: nothing here to act on (no track or
    // reason, or not the track being started); the caller shows the error.
    bool handleTrackError(const QString& sourceId, const QJsonObject& data);

    void togglePause();
    void stop();
    // Stops like stop(), but not by choice: the current track's source went
    // away. No halted() — playback was still wanted.
    void dropCurrentTrack();
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);
    // Music turned down while the app plays a sound of its own over it.
    void setDucked(bool ducked);
    // See AudioPlayer::setEdges() and setOutput().
    void setEdges(int fadeMs, int settleMs);
    void setOutput(const QByteArray& output, int openslesFramesPerEnqueue);
#ifdef Q_OS_ANDROID
    // See AudioPlayer::setAudioSession().
    void setAudioSession(int sessionId);
#endif

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
    // A track is on its way to playing: resolved, or waiting for its source.
    bool isStarting() const { return startingIndex_ >= 0 || awaitedIndex_ >= 0; }
    const Track& currentTrack() const { return queue_[index_].track; }
    const QString& currentSourceId() const { return queue_[index_].sourceId; }
    const QVector<QueueEntry>& queue() const { return queue_; }
    int currentIndex() const { return index_; }
    // Which entry next() (1) or previous() (-1) would go to, changing nothing; -1
    // if there's none: the end of the list, or where a shuffled list would start
    // another round, whose order isn't known yet.
    int neighborIndex(int delta) const;

    // A service said the track can't be played; it's skipped for
    // kUnavailableForMs, then tried again.
    void setUnavailable(const QString& sourceId, const QString& trackId);
    bool isUnavailable(const QString& sourceId, const QString& trackId) const;
    // The listener asked for the track itself: it is tried again.
    void forgetUnavailable(const QString& sourceId, const QString& trackId);
    static constexpr qint64 kUnavailableForMs = 10 * 60 * 1000;

signals:
    void trackChanged(const Track& track, const QString& sourceId);
    void playingChanged(bool playing);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    // How far into the track it is downloaded — all of it for one that
    // plays from the cache; -1: nothing to show (a source's own local file).
    void bufferedChanged(qint64 bufferedMs);
    void seeked(qint64 positionMs);
    // A source's stream for a track — the playing one, or one resolved for
    // preloading; says what it is (codec, bitrate) for the track's details.
    void streamResolved(const QString& sourceId, const QString& trackId, const StreamDescriptor& stream);
    void loadingChanged(bool loading); // waiting on track/streamReady — drives the busy indicator
    // A source said it can't reach its service.
    void offlineSeen(const QString& sourceId);
    // A radio's trackFinished or skip that the source can't be told now:
    // `params` carry playedAt and deferred, to be sent late (docs/protocol.md
    // §7.4).
    void feedbackDeferred(const QString& sourceId, const QString& method, const QJsonObject& params);
    // A track outside a radio played long enough to count (30 seconds, or
    // half of it): for the service's history (feedback.played).
    void trackPlayed(const QString& sourceId, const Track& track, int playedMs, const QDateTime& startedAt);
    // What to say about it: not an error if a view shows that state.
    void offlineNotice(const QString& message);
    void errorOccurred(QString message);
    // A source said this track can't be played at all (reason "unavailable").
    void trackUnavailable(const QString& sourceId, const QString& trackId);
    // ...and it is to be tried again (forgetUnavailable()).
    void trackAvailableAgain(const QString& sourceId, const QString& trackId);
    // The same failure, for statistics: the source it was on and a fixed
    // reason — "unavailable" (the source is down), "rejected" (the source
    // refused to play the track), "stream" (the audio didn't start),
    // "timeout", "station_empty", "audio_output" (no audio output could be
    // opened).
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
    // The tracks being preloaded now: the playing (or starting) one and the
    // ones after it, as many as the preload depth — so that what goes with
    // them (their covers) can be fetched ahead too. Comes often, mostly
    // with the same tracks.
    void preloadWindowChanged(const QVector<QueueEntry>& entries);

private:
    int insertNext(QueueEntry entry);
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
    void handleStartFailure(
        const QString& message, const QString& reason, AudioPlayer::Failure failure = AudioPlayer::Failure::Stream);
    void reportError(const QString& sourceId, const QString& reason, const QString& message);
    // Nothing plays now: shown as paused, Play loads the track again.
    void stopAfterFailure();
    // The track can't be started for lack of a network: on to the next one
    // that's on disk, or paused here.
    void goOnOffline(int index, const QString& sourceId);
    // The first track after `from`, in play order, that isn't known to be
    // unavailable — and, with `downloadedOnly`, is downloaded; -1 if none
    // (or if skipping has already gone through the whole queue without a
    // track starting).
    int nextPlayableAfter(int from, bool downloadedOnly) const;
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
    // The radio, topped up from the disk (offlineRadio_): the next index to
    // play after `from`, or -1 if there's nothing.
    int fillRadioFromCache(int from);
    // trackPlayed(), once per play of the current track, if it counts.
    void reportPlayed();
    // Into feedbackDeferred(), with when it was played.
    void deferFeedback(const QString& sourceId, const QString& method, QJsonObject params, int playedMs);
    // Sends a radio's feedback once what was sent before it is through
    // (see radioFeedback_).
    // `deferOffline`: what to do instead when it fails for lack of a network.
    Rpc::Task<void> sendRadioFeedback(QString sourceId, std::function<Rpc::Task<void>(Rpc::RpcClient&)> send,
        std::function<void()> deferOffline = { });
    // How many tracks are queued after `index` — told to a radio with the
    // track's feedback (queuedAfter, docs/protocol.md §7.1): the station
    // tops the queue up by it, as only this queue knows what it has left.
    int queuedAfter(int index) const;

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
    FeedbackHold feedbackHold_;
    OfflineRadioProvider offlineRadio_;
    ReachableCheck reachable_;
    // Topped up from the disk with nothing started since: not again, or a
    // cache of files that won't play would spin.
    bool filledFromCache_ = false;
    // The current track's play went out as trackPlayed() already.
    bool playReported_ = false;

    int latestRequestId_ = -1;
    int transitionGeneration_ = 0;
    // The station follows the listener by its feedback, and a source
    // handles requests in no particular order: a skip must be through
    // before the next track's start is sent.
    Rpc::Sequence radioFeedback_;
    std::optional<StreamReadyParams> earlyStreamReady_;
    // Same for a track error (see handleTrackError()): sourceId and data.
    std::optional<std::pair<QString, QJsonObject>> earlyTrackError_;
    // The playing track's stream as playback.play resolved it; none when
    // it plays from the cache.
    std::optional<StreamDescriptor> currentStream_;
    int preparedIndex_ = -1;
    bool preparedReady_ = false;
    bool preparedStartPending_ = false;
    // Fresh resolutions tried for the current track since it last started.
    int startRetries_ = 0;
    // Tracks skipped one after another since a track last started; bounds
    // the skipping to one pass over the queue.
    int skipStreak_ = 0;
    // Tracks a source said it can't play (sourceId + 0x1f + trackId), this run.
    // When each was told unavailable (ms since the epoch): a service says so
    // of tracks that play later, so it holds only for kUnavailableForMs, or
    // until the listener picks the track — see forgetUnavailable().
    QHash<QString, qint64> unavailable_;
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
    // A track that has to come from a source that is still starting: it
    // plays once the source is up (see sourceCameUp()), or is given up on
    // after kSourceWaitMs. -1 if none.
    int awaitedIndex_ = -1;
    QString awaitedSourceId_;
    int awaitedTransition_ = 0;
    QTimer* sourceWaitTimer_ = nullptr;
    static constexpr int kSourceWaitMs = 30000;
    void playAwaited();
    void clearAwaited();
    int startTimeoutMs_ = 10000;
    qint64 lastKnownPositionMs_ = 0;
    QString resumeTrackId_;
    qint64 resumePositionMs_ = 0;
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
