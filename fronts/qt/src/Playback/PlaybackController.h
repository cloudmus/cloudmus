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
    void startRadio(const QString& sourceId, const QString& stationId, const QList<Track>& initialTracks);

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
    const Track& currentTrack() const { return queue_[index_].track; }
    const QString& currentSourceId() const { return queue_[index_].sourceId; }

signals:
    void trackChanged(const Track& track, const QString& sourceId);
    void playingChanged(bool playing);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void loadingChanged(bool loading); // waiting on track/streamReady — drives the busy indicator
    void errorOccurred(QString message);

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
    QString latestRequestSourceId_;
    QTimer* playTimeoutTimer_ = nullptr;
    qint64 lastKnownPositionMs_ = 0;

    bool waveMode_ = false;
    QString waveSourceId_;
    QString waveStationId_;
};

} // namespace Playback
