#include "PlaybackController.h"

#include <QTimer>

#include "AudioPlayer.h"
#include "RpcMethods.h"

namespace Playback {

namespace {
constexpr int kPlayTimeoutMs = 10000;
}

PlaybackController::PlaybackController(Rpc::SourceManager& sourceManager, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , audioPlayer_(new AudioPlayer(this))
{
    connect(audioPlayer_, &AudioPlayer::endOfFile, this, [this]() { advance(1, /*wasSkip=*/false); });
    connect(audioPlayer_, &AudioPlayer::started, this, [this]() {
        playTimeoutTimer_->stop();
        emit loadingChanged(false);
        playing_ = true;
        emit playingChanged(true);
    });
    connect(audioPlayer_, &AudioPlayer::failed, this, [this](const QString& message) {
        playTimeoutTimer_->stop();
        emit loadingChanged(false);
        emit errorOccurred(message);
    });
    connect(audioPlayer_, &AudioPlayer::positionChanged, this, [this](qint64 posMs, qint64 durMs) {
        lastKnownPositionMs_ = posMs;
        emit positionChanged(posMs, durMs);
    });

    playTimeoutTimer_ = new QTimer(this);
    playTimeoutTimer_->setSingleShot(true);
    playTimeoutTimer_->setInterval(kPlayTimeoutMs);
    connect(playTimeoutTimer_, &QTimer::timeout, this, [this]() {
        emit loadingChanged(false);
        emit errorOccurred(QStringLiteral("Timed out waiting for the track to start"));
    });
}

PlaybackController::~PlaybackController() = default;

void PlaybackController::loadQueue(const QString& sourceId, const QList<Track>& tracks, int startIndex)
{
    waveMode_ = false;
    queue_.clear();
    queue_.reserve(tracks.size());
    for (const Track& t : tracks) {
        queue_.append(QueueEntry { sourceId, t });
    }
    emit queueAvailabilityChanged(hasQueue());
    playIndex(startIndex);
}

void PlaybackController::startRadio(
    const QString& sourceId, const QString& stationId, const QList<Track>& initialTracks)
{
    waveMode_ = true;
    waveSourceId_ = sourceId;
    waveStationId_ = stationId;
    queue_.clear();
    queue_.reserve(initialTracks.size());
    for (const Track& t : initialTracks) {
        queue_.append(QueueEntry { sourceId, t });
    }
    emit queueAvailabilityChanged(hasQueue());
    playIndex(0);
}

void PlaybackController::enqueueNext(const QString& sourceId, const Track& track)
{
    const bool wasEmpty = !hasQueue();
    const int insertPos = hasCurrentTrack() ? index_ + 1 : 0;
    queue_.insert(insertPos, QueueEntry { sourceId, track });
    if (wasEmpty)
        emit queueAvailabilityChanged(true);
    if (!hasCurrentTrack())
        playIndex(0);
}

void PlaybackController::enqueueAtEnd(const QString& sourceId, const Track& track)
{
    const bool wasEmpty = !hasQueue();
    const bool shouldPlayImmediately = !hasCurrentTrack();
    queue_.append(QueueEntry { sourceId, track });
    if (wasEmpty)
        emit queueAvailabilityChanged(true);
    // Play the entry just appended (queue_.size() - 1), not index 0 — stop()
    // leaves hasCurrentTrack() false without clearing queue_, so index 0
    // could be a stale leftover entry from before Stop was pressed rather
    // than the track this call is actually about.
    if (shouldPlayImmediately)
        playIndex(queue_.size() - 1);
}

void PlaybackController::handleTracksAdded(const QString& sourceId, const TracksAddedParams& params)
{
    if (!waveMode_ || sourceId != waveSourceId_ || params.stationId != waveStationId_)
        return;
    for (const Track& t : params.tracks) {
        queue_.append(QueueEntry { sourceId, t });
    }
}

void PlaybackController::playIndex(int index) { playIndexAsync(index).detach(); }

Rpc::Task<void> PlaybackController::playIndexAsync(int index)
{
    if (index < 0 || index >= queue_.size())
        co_return;

    Rpc::RpcClient* client = sourceManager_.client(queue_[index].sourceId);
    if (client == nullptr || !client->available()) {
        emit errorOccurred(QStringLiteral("Source is unavailable"));
        co_return;
    }

    // By value, not const&: queue_ is a QVector, and handleTracksAdded()
    // (driven by a radio/tracksAdded notification that can arrive at any
    // time — including while this coroutine is suspended at the co_await
    // below) appends to it, which can reallocate the backing storage and
    // dangle a reference into it. That's exactly the failure this used to
    // have: playback.play still succeeds (it only needs entry.track.id,
    // read before the suspension) and the actual audio plays fine (that
    // comes from a separate track/streamReady notification, not from
    // `entry`), but trackChanged(entry.track, ...) below — which drives the
    // hero panel's title/cover — could fire with a dangling QueueEntry,
    // showing garbage or blank metadata for a track that's audibly playing.
    const QueueEntry entry = queue_[index];
    const int id = client->allocateRequestId();
    latestRequestId_ = id;
    latestRequestSourceId_ = entry.sourceId;
    emit loadingChanged(true);
    playTimeoutTimer_->start();

    try {
        PlayParams params { entry.track.id };
        co_await client->callWithId<PlayResult>(id, QStringLiteral("playback.play"), params.toJson(), 5000);
    } catch (const Rpc::RpcCallException& e) {
        if (id == latestRequestId_) {
            playTimeoutTimer_->stop();
            emit loadingChanged(false);
            emit errorOccurred(QString::fromStdString(e.error().message.toStdString()));
        }
        co_return;
    }

    const bool wasCurrentTrack = hasCurrentTrack();
    index_ = index;
    if (!wasCurrentTrack)
        emit currentTrackAvailabilityChanged(true);
    if (waveMode_) {
        TrackStartedParams started { entry.track.id };
        Rpc::feedbackTrackStarted(*client, started).detach();
    }
    emit trackChanged(entry.track, entry.sourceId);
}

void PlaybackController::handleStreamReady(const QString& sourceId, const StreamReadyParams& params)
{
    if (params.requestId != latestRequestId_ || sourceId != latestRequestSourceId_) {
        return; // superseded by a later playback.play — discard, see docs/protocol.md §11.1
    }
    // Displayed by AudioPlayer as the track identity in the OS's per-stream
    // audio widget (mpv would otherwise report itself as "mpv" playing a
    // title derived from the raw stream URL) — same "%1 — %2" convention as
    // TrayIcon::setNowPlayingTooltip.
    const Track& track = currentTrack();
    QString title = track.title;
    if (!track.artists.isEmpty()) {
        QString artists;
        for (int i = 0; i < track.artists.size(); ++i) {
            if (i > 0)
                artists += QStringLiteral(", ");
            artists += track.artists[i].name;
        }
        title = QStringLiteral("%1 — %2").arg(track.title, artists);
    }

    // Loading indicator / playTimeoutTimer_ stay active until
    // AudioPlayer::started() or failed() — play() is asynchronous now (see
    // AudioPlayer.h), it may still be downloading the stream.
    audioPlayer_->play(params.stream.url, title);
}

void PlaybackController::advance(int delta, bool wasSkip)
{
    if (queue_.isEmpty())
        return;
    sendFeedbackFinishedOrSkip(wasSkip);
    int nextIndex = index_ + delta;
    if (nextIndex < 0)
        nextIndex = 0;
    if (nextIndex >= queue_.size()) {
        if (waveMode_)
            return; // wait for radio/tracksAdded to top up the queue
        nextIndex = queue_.size() - 1;
    }
    playIndex(nextIndex);
}

void PlaybackController::sendFeedbackFinishedOrSkip(bool wasSkip)
{
    if (!waveMode_ || !hasCurrentTrack())
        return;
    Rpc::RpcClient* client = sourceManager_.client(currentSourceId());
    if (client == nullptr || !client->available())
        return;
    const int playedMs = static_cast<int>(lastKnownPositionMs_);
    if (wasSkip) {
        SkipParams params { currentTrack().id, playedMs };
        Rpc::feedbackSkip(*client, params).detach();
    } else {
        TrackFinishedParams params { currentTrack().id, playedMs };
        Rpc::feedbackTrackFinished(*client, params).detach();
    }
}

void PlaybackController::togglePause()
{
    if (!hasCurrentTrack())
        return;
    if (playing_) {
        audioPlayer_->pause();
        playing_ = false;
    } else {
        audioPlayer_->resume();
        playing_ = true;
    }
    emit playingChanged(playing_);
}

void PlaybackController::stop()
{
    audioPlayer_->stop();
    playTimeoutTimer_->stop();
    // Discards any in-flight playback.play this stop() interrupts —
    // without this, a track/streamReady that arrives after index_ is
    // reset below would pass handleStreamReady()'s requestId check (it's
    // still "the latest" — nothing superseded it, the user just stopped)
    // and then dereference queue_[index_] at index_ == -1.
    latestRequestId_ = -1;
    const bool hadCurrentTrack = hasCurrentTrack();
    index_ = -1; // the current track becomes undefined — see hasCurrentTrack()
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
    if (hadCurrentTrack)
        emit currentTrackAvailabilityChanged(false);
}

void PlaybackController::next() { advance(1, /*wasSkip=*/true); }

void PlaybackController::previous() { advance(-1, /*wasSkip=*/true); }

void PlaybackController::seek(qint64 positionMs) { audioPlayer_->seek(positionMs); }

void PlaybackController::setVolume(int volume0To100) { audioPlayer_->setVolume(volume0To100); }

void PlaybackController::setTrackLiked(const QString& sourceId, const QString& trackId, bool liked)
{
    for (QueueEntry& entry : queue_) {
        if (entry.sourceId == sourceId && entry.track.id == trackId)
            entry.track.liked = liked;
    }
}

} // namespace Playback
