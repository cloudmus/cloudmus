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
    playIndex(startIndex);
}

void PlaybackController::startRadio(const QString& sourceId, const QString& stationId,
                                    const QList<Track>& initialTracks)
{
    waveMode_ = true;
    waveSourceId_ = sourceId;
    waveStationId_ = stationId;
    queue_.clear();
    queue_.reserve(initialTracks.size());
    for (const Track& t : initialTracks) {
        queue_.append(QueueEntry { sourceId, t });
    }
    playIndex(0);
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
    // now-playing bar's title/cover — could fire with a dangling QueueEntry,
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

    index_ = index;
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
    // Loading indicator / playTimeoutTimer_ stay active until
    // AudioPlayer::started() or failed() — play() is asynchronous now (see
    // AudioPlayer.h), it may still be downloading the stream.
    audioPlayer_->play(params.stream.url);
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
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
}

void PlaybackController::next() { advance(1, /*wasSkip=*/true); }

void PlaybackController::previous() { advance(-1, /*wasSkip=*/true); }

void PlaybackController::seek(qint64 positionMs) { audioPlayer_->seek(positionMs); }

void PlaybackController::setVolume(int volume0To100) { audioPlayer_->setVolume(volume0To100); }

} // namespace Playback
