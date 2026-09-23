#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "Coro.h"
#include "Models.h"
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
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);

    bool isPlaying() const { return playing_; }
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

private:
    void playIndex(int index);
    Rpc::Task<void> playIndexAsync(int index);
    void advance(int delta, bool wasSkip);
    void sendFeedbackFinishedOrSkip(bool wasSkip);

    Rpc::SourceManager& sourceManager_;
    AudioPlayer* audioPlayer_ = nullptr;

    QVector<QueueEntry> queue_;
    int index_ = -1;
    bool playing_ = false;

    int latestRequestId_ = -1;
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

    bool waveMode_ = false;
    QString waveSourceId_;
    QString waveStationId_;
};

} // namespace Playback
