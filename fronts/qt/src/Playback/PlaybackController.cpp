#include "PlaybackController.h"

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#include <algorithm>

#include "AudioPlayer.h"
#include "RpcMethods.h"

namespace Playback {

namespace {
constexpr int kPlayTimeoutMs = 10000;
// A stream URL can be refused although it was just resolved (YouTube's
// googlevideo answers some with 403 Forbidden); a fresh one usually plays.
constexpr int kMaxStartRetries = 2;
Q_LOGGING_CATEGORY(lcPlayback, "cloudmus.playback")
}

PlaybackController::PlaybackController(Rpc::SourceManager& sourceManager, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , audioPlayer_(new AudioPlayer(this))
{
    connect(audioPlayer_, &AudioPlayer::endOfFile, this, [this]() { advance(1, /*wasSkip=*/false); });
    connect(audioPlayer_, &AudioPlayer::started, this, [this]() {
        preparedStartPending_ = false;
        startRetries_ = 0;
        reloadOnResume_ = false;
        playTimeoutTimer_->stop();
        emit loadingChanged(false);
        playing_ = true;
        emit playingChanged(true);
        prepareNext();
    });
    connect(audioPlayer_, &AudioPlayer::failed, this, &PlaybackController::handleStartFailure);
    connect(audioPlayer_, &AudioPlayer::positionChanged, this, [this](qint64 posMs, qint64 durMs) {
        lastKnownPositionMs_ = posMs;
        emit positionChanged(posMs, durMs);
    });
    connect(audioPlayer_, &AudioPlayer::bufferedChanged, this, &PlaybackController::bufferedChanged);
    connect(audioPlayer_, &AudioPlayer::prepared, this, [this]() { preparedReady_ = true; });

    playTimeoutTimer_ = new QTimer(this);
    playTimeoutTimer_->setSingleShot(true);
    playTimeoutTimer_->setInterval(kPlayTimeoutMs);
    connect(playTimeoutTimer_, &QTimer::timeout, this,
        [this]() { handleStartFailure(QStringLiteral("Timed out waiting for the track to start")); });
}

PlaybackController::~PlaybackController() = default;

qint64 PlaybackController::positionMs() const
{
    return hasCurrentTrack() ? std::clamp(lastKnownPositionMs_, qint64(0), qint64(currentTrack().durationMs)) : 0;
}

void PlaybackController::loadQueue(const QString& sourceId, const QList<Track>& tracks, int startIndex)
{
    QVector<QueueEntry> entries;
    entries.reserve(tracks.size());
    for (const Track& t : tracks)
        entries.append(QueueEntry { sourceId, t });
    loadQueue(entries, startIndex);
}

void PlaybackController::loadQueue(const QVector<QueueEntry>& entries, int startIndex)
{
    invalidatePrepared();
    setWaveMode(false);
    awaitingRadioTracks_ = false;
    queue_ = entries;
    if (shuffleActive())
        reshuffle(startIndex);
    emit queueAvailabilityChanged(hasQueue());
    emit queueChanged();
    playIndex(startIndex);
}

void PlaybackController::handleStartFailure(const QString& message)
{
    preparedStartPending_ = false;
    // Also an expired prefetched URL: resolving the track again gets a
    // fresh one.
    if (hasCurrentTrack() && startRetries_ < kMaxStartRetries) {
        ++startRetries_;
        qCDebug(lcPlayback) << "track didn't start, resolving it again:" << message;
        playIndex(index_);
        return;
    }
    startRetries_ = 0;
    playTimeoutTimer_->stop();
    emit loadingChanged(false);
    // Nothing plays now: shown as paused, and Play loads the track again
    // rather than resuming an mpv with nothing in it.
    reloadOnResume_ = hasCurrentTrack();
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
    emit errorOccurred(message);
}

void PlaybackController::playAt(int index)
{
    startRetries_ = 0;
    awaitingRadioTracks_ = false;
    // Jumping away from the current track is a skip as far as a radio is
    // concerned — and its feedback is what makes the station send the next
    // tracks, so without it jumping to the last queued track dead-ends.
    if (index != index_)
        sendFeedbackFinishedOrSkip(/*wasSkip=*/true);
    playIndex(index);
}

void PlaybackController::startRadio(
    const QString& sourceId, const QString& stationId, const QList<Track>& initialTracks)
{
    invalidatePrepared();
    setWaveMode(true);
    awaitingRadioTracks_ = false;
    waveSourceId_ = sourceId;
    waveStationId_ = stationId;
    queue_.clear();
    queue_.reserve(initialTracks.size());
    for (const Track& t : initialTracks) {
        queue_.append(QueueEntry { sourceId, t });
    }
    emit queueAvailabilityChanged(hasQueue());
    emit queueChanged();
    playIndex(0);
}

void PlaybackController::enqueueNext(const QString& sourceId, const Track& track)
{
    invalidatePrepared();
    const bool wasEmpty = !hasQueue();
    const int insertPos = hasCurrentTrack() ? index_ + 1 : 0;
    queue_.insert(insertPos, QueueEntry { sourceId, track, /*userQueued=*/true });
    if (shuffleActive()) {
        // "Play Next" means next in play order too.
        for (int& i : order_) {
            if (i >= insertPos)
                ++i;
        }
        order_.insert(order_.indexOf(index_) + 1, insertPos);
    }
    if (wasEmpty)
        emit queueAvailabilityChanged(true);
    emit queueChanged();
    if (!hasCurrentTrack())
        playIndex(0);
    else
        prepareNext();
}

void PlaybackController::enqueueAtEnd(const QString& sourceId, const Track& track)
{
    invalidatePrepared();
    const bool wasEmpty = !hasQueue();
    const bool shouldPlayImmediately = !hasCurrentTrack();
    queue_.append(QueueEntry { sourceId, track, /*userQueued=*/true });
    if (shuffleActive())
        order_.append(queue_.size() - 1);
    if (wasEmpty)
        emit queueAvailabilityChanged(true);
    emit queueChanged();
    // Play the entry just appended (queue_.size() - 1), not index 0 — stop()
    // leaves hasCurrentTrack() false without clearing queue_, so index 0
    // could be a stale leftover entry from before Stop was pressed rather
    // than the track this call is actually about.
    if (shouldPlayImmediately)
        playIndex(queue_.size() - 1);
    else
        prepareNext();
}

void PlaybackController::handleTracksAdded(const QString& sourceId, const TracksAddedParams& params)
{
    if (!waveMode_ || sourceId != waveSourceId_ || params.stationId != waveStationId_)
        return;
    invalidatePrepared();
    if (params.replaceUpcoming.value_or(false)) {
        // The station recomputed what comes next (docs/protocol.md §7.1):
        // drop the station's own unplayed tracks after the current (or
        // starting) one, keep what the user queued, then append the new
        // sequence minus anything already in the played part.
        const int keep = qMin(qMax(index_, startingIndex_), int(queue_.size()) - 1);
        QSet<QString> playedIds;
        for (int i = 0; i <= keep; ++i)
            playedIds.insert(queue_[i].track.id);
        QVector<QueueEntry> userQueued;
        for (int i = keep + 1; i < queue_.size(); ++i) {
            if (queue_[i].userQueued)
                userQueued.append(queue_[i]);
        }
        queue_.resize(keep + 1);
        queue_ += userQueued;
        for (const Track& t : params.tracks) {
            if (!playedIds.contains(t.id))
                queue_.append(QueueEntry { sourceId, t });
        }
    } else {
        for (const Track& t : params.tracks)
            queue_.append(QueueEntry { sourceId, t });
    }
    emit queueChanged();
    if (awaitingRadioTracks_ && index_ + 1 < queue_.size()) {
        awaitingRadioTracks_ = false;
        playIndex(index_ + 1);
    } else {
        prepareNext();
    }
}

void PlaybackController::playIndex(int index) { playIndexAsync(index).detach(); }

Rpc::Task<void> PlaybackController::playIndexAsync(int index)
{
    if (index < 0 || index >= queue_.size())
        co_return;

    // A slow backend or the redirect preflight must not leave the old track
    // audible after the user has selected a new one.
    stopForTransition();

    Rpc::RpcClient* client = sourceManager_.client(queue_[index].sourceId);
    if (client == nullptr || !client->available()) {
        emit loadingChanged(false);
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
    const QPointer<PlaybackController> alive(this);
    const int transition = transitionGeneration_;
    const int id = client->allocateRequestId();
    latestRequestId_ = id;
    latestRequestSourceId_ = entry.sourceId;
    startingIndex_ = index;
    emit loadingChanged(true);
    playTimeoutTimer_->start();

    try {
        PlayParams params { entry.track.id };
        co_await client->callWithId<PlayResult>(id, QStringLiteral("playback.play"), params.toJson(), 5000);
    } catch (const Rpc::RpcCallException& e) {
        if (!alive)
            co_return;
        if (transition == transitionGeneration_ && id == latestRequestId_) {
            startingIndex_ = -1;
            playTimeoutTimer_->stop();
            emit loadingChanged(false);
            emit errorOccurred(QString::fromStdString(e.error().message.toStdString()));
        }
        co_return;
    }

    if (!alive)
        co_return;

    if (transition != transitionGeneration_ || id != latestRequestId_)
        co_return;

    const bool wasCurrentTrack = hasCurrentTrack();
    index_ = index;
    lastKnownPositionMs_ = 0;
    if (id == latestRequestId_)
        startingIndex_ = -1;
    if (!wasCurrentTrack)
        emit currentTrackAvailabilityChanged(true);
    if (waveMode_) {
        TrackStartedParams started { entry.track.id };
        Rpc::feedbackTrackStarted(*client, started).detach();
    }
    emit trackChanged(entry.track, entry.sourceId);
    if (earlyStreamReady_) {
        const StreamReadyParams ready = *earlyStreamReady_;
        earlyStreamReady_.reset();
        handleStreamReady(entry.sourceId, ready);
    }
}

void PlaybackController::handleStreamReady(const QString& sourceId, const StreamReadyParams& params)
{
    if (params.requestId != latestRequestId_ || sourceId != latestRequestSourceId_) {
        return; // superseded by a later playback.play — discard, see docs/protocol.md §11.1
    }
    if (startingIndex_ >= 0) {
        earlyStreamReady_ = params;
        return;
    }
    // Displayed by AudioPlayer as the track identity in the OS's per-stream
    // audio widget (mpv would otherwise report itself as "mpv" playing a
    // title derived from the raw stream URL) — same "%1 — %2" convention as
    // TrayIcon::setNowPlayingTooltip.
    const QString title = titleFor(currentTrack());

    // Loading indicator / playTimeoutTimer_ stay active until
    // AudioPlayer::started() or failed() — play() is asynchronous now (see
    // AudioPlayer.h), it may still be downloading the stream.
    audioPlayer_->play(params.stream.url, title,
        streamRouteProvider_ ? streamRouteProvider_(queue_[index_].sourceId) : std::nullopt,
        params.stream.headers.value_or(QMap<QString, QString> { }));
}

void PlaybackController::advance(int delta, bool wasSkip)
{
    startRetries_ = 0;
    if (queue_.isEmpty())
        return;
    sendFeedbackFinishedOrSkip(wasSkip);
    if (!wasSkip && effectiveRepeatMode() == RepeatMode::One && hasCurrentTrack()) {
        if (preparedReady_ && preparedIndex_ == index_)
            promotePrepared(index_, false);
        else
            playIndex(index_);
        return;
    }
    // Reuse the prepared index rather than stepping again: stepFrom() may
    // reshuffle when it wraps. Under Repeat One the prepared entry is the
    // current track itself, which a manual Next must skip.
    const bool preparedIsNext = preparedIndex_ >= 0 && effectiveRepeatMode() != RepeatMode::One;
    const int nextIndex = delta == 1 && preparedIsNext ? preparedIndex_ : stepFrom(delta);
    if (nextIndex >= 0) {
        const bool sameRoute
            = !streamRouteProvider_ || preparedRoute_ == streamRouteProvider_(queue_[nextIndex].sourceId);
        if (delta == 1 && preparedReady_ && nextIndex == preparedIndex_ && sameRoute) {
            qCDebug(lcPlayback) << "advancing to the prepared track" << nextIndex;
            promotePrepared(nextIndex, wasSkip);
        } else {
            qCDebug(lcPlayback) << "advancing to an unprepared track" << nextIndex << "prepared:" << preparedIndex_
                                << "ready:" << preparedReady_ << "same route:" << sameRoute;
            playIndex(nextIndex);
        }
        return;
    }
    if (delta < 0) {
        // Before the first track: start the current one over.
        playIndex(hasCurrentTrack() ? index_ : 0);
        return;
    }
    if (waveMode_) {
        // The feedback just sent makes the station push more tracks;
        // handleTracksAdded() picks playback up from there.
        awaitingRadioTracks_ = true;
        emit loadingChanged(true);
        // Don't spin forever if the station has nothing more to send.
        const int generation = ++radioWaitGeneration_;
        QTimer::singleShot(15000, this, [this, generation]() {
            if (!awaitingRadioTracks_ || generation != radioWaitGeneration_)
                return;
            awaitingRadioTracks_ = false;
            emit loadingChanged(false);
            emit errorOccurred(QStringLiteral("The station sent no more tracks"));
        });
        return;
    }
    // The end of the list: a finished last track stops playback; Next
    // there has nowhere to go.
    if (!wasSkip)
        stop();
}

int PlaybackController::stepFrom(int delta)
{
    const int count = int(queue_.size());
    if (count == 0)
        return -1;
    const bool wrap = effectiveRepeatMode() == RepeatMode::All;
    if (shuffleActive() && order_.size() == count) {
        const int pos = hasCurrentTrack() ? int(order_.indexOf(index_)) : -1;
        const int nextPos = pos + delta;
        if (nextPos >= 0 && nextPos < count)
            return order_[nextPos];
        if (!wrap)
            return -1;
        if (nextPos >= count) {
            // Another round, in a new order.
            reshuffle(-1);
            if (count > 1 && order_.first() == index_)
                std::swap(order_[0], order_[count - 1]);
            return order_.first();
        }
        return order_.last();
    }
    const int nextIndex = (hasCurrentTrack() ? index_ : -1) + delta;
    if (nextIndex >= 0 && nextIndex < count)
        return nextIndex;
    if (!wrap)
        return -1;
    return nextIndex < 0 ? count - 1 : 0;
}

void PlaybackController::reshuffle(int first)
{
    order_.clear();
    for (int i = 0; i < queue_.size(); ++i) {
        if (i != first)
            order_.append(i);
    }
    std::shuffle(order_.begin(), order_.end(), *QRandomGenerator::global());
    if (first >= 0 && first < queue_.size())
        order_.prepend(first);
}

void PlaybackController::setShuffle(bool on)
{
    if (shuffle_ == on)
        return;
    shuffle_ = on;
    if (shuffleActive())
        reshuffle(index_); // the rest of the list shuffled after what's playing
    else
        order_.clear();
    invalidatePrepared();
    prepareNext();
    emit playModeChanged();
}

void PlaybackController::setRepeatMode(RepeatMode mode)
{
    if (repeatMode_ == mode)
        return;
    repeatMode_ = mode;
    invalidatePrepared();
    prepareNext();
    emit playModeChanged();
}

void PlaybackController::setWaveMode(bool on)
{
    order_.clear(); // rebuilt by loadQueue() once the new queue is in
    if (waveMode_ == on)
        return;
    waveMode_ = on;
    emit playModeChanged();
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
    if (reloadOnResume_) {
        reloadOnResume_ = false;
        playIndex(index_);
        return;
    }
    if (playing_) {
        audioPlayer_->pause();
        playing_ = false;
    } else {
        audioPlayer_->resume();
        playing_ = true;
    }
    emit playingChanged(playing_);
    if (!playing_)
        emit halted();
}

void PlaybackController::stop()
{
    dropCurrentTrack();
    emit halted();
}

void PlaybackController::dropCurrentTrack()
{
    stopForTransition();
    // Discards any in-flight playback.play this stop() interrupts —
    // without this, a track/streamReady that arrives after index_ is
    // reset below would pass handleStreamReady()'s requestId check (it's
    // still "the latest" — nothing superseded it, the user just stopped)
    // and then dereference queue_[index_] at index_ == -1.
    startingIndex_ = -1;
    if (awaitingRadioTracks_) {
        awaitingRadioTracks_ = false;
    }
    emit loadingChanged(false);
    const bool hadCurrentTrack = hasCurrentTrack();
    index_ = -1; // the current track becomes undefined — see hasCurrentTrack()
    lastKnownPositionMs_ = 0;
    if (hadCurrentTrack)
        emit currentTrackAvailabilityChanged(false);
}

void PlaybackController::stopForTransition()
{
    preparedStartPending_ = false;
    reloadOnResume_ = false;
    ++transitionGeneration_;
    invalidatePrepared();
    audioPlayer_->stop();
    playTimeoutTimer_->stop();
    latestRequestId_ = -1;
    startingIndex_ = -1;
    earlyStreamReady_.reset();
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
}

QString PlaybackController::titleFor(const Track& track) const
{
    if (track.artists.isEmpty())
        return track.title;
    QStringList artists;
    for (const auto& artist : track.artists)
        artists.append(artist.name);
    return QStringLiteral("%1 — %2").arg(track.title, artists.join(QStringLiteral(", ")));
}

void PlaybackController::invalidatePrepared()
{
    if (preparedRequestId_ >= 0) {
        if (Rpc::RpcClient* client = preparedClient_) {
            CancelParams params { preparedRequestId_ };
            Rpc::playbackCancel(*client, params).detach();
        }
    }
    preparedRequestId_ = -1;
    preparedClient_.clear();
    ++preparedGeneration_;
    preparedIndex_ = -1;
    preparedReady_ = false;
    preparedRoute_.reset();
    audioPlayer_->clearPrepared();
}

void PlaybackController::prepareNext()
{
    if (!playing_ || !hasCurrentTrack() || preparedIndex_ >= 0 || queue_.isEmpty())
        return;
    const int nextIndex = effectiveRepeatMode() == RepeatMode::One ? index_ : stepFrom(1);
    if (nextIndex < 0 || nextIndex >= queue_.size())
        return;
    const QueueEntry entry = queue_[nextIndex];
    Rpc::RpcClient* client = sourceManager_.client(entry.sourceId);
    if (!client || !client->available())
        return;
    const QJsonObject playbackCaps = client->capabilities().value(QStringLiteral("playback")).toObject();
    if (!playbackCaps.value(QStringLiteral("resolveStream")).toBool()) {
        qCDebug(lcPlayback) << "source can't resolve streams ahead, no preloading:" << entry.sourceId;
        return;
    }
    qCDebug(lcPlayback) << "preparing the next track" << nextIndex << entry.track.id;
    preparedIndex_ = nextIndex;
    prepareNextAsync(++preparedGeneration_, nextIndex, entry).detach();
}

Rpc::Task<void> PlaybackController::prepareNextAsync(int generation, int nextIndex, QueueEntry entry)
{
    const QPointer<PlaybackController> alive(this);
    Rpc::RpcClient* client = sourceManager_.client(entry.sourceId);
    if (!client)
        co_return;
    const int id = client->allocateRequestId();
    preparedRequestId_ = id;
    preparedClient_ = client;
    try {
        ResolveStreamParams params { entry.track.id };
        ResolveStreamResult result = co_await client->callWithId<ResolveStreamResult>(
            id, QStringLiteral("playback.resolveStream"), params.toJson(), 20000);
        if (!alive)
            co_return;
        if (generation != preparedGeneration_ || nextIndex >= queue_.size()
            || queue_[nextIndex].sourceId != entry.sourceId || queue_[nextIndex].track.id != entry.track.id)
            co_return;
        preparedRequestId_ = -1;
        preparedClient_.clear();
        preparedRoute_ = streamRouteProvider_ ? streamRouteProvider_(entry.sourceId) : std::nullopt;
        audioPlayer_->prepare(result.stream.url, titleFor(entry.track), preparedRoute_,
            result.stream.headers.value_or(QMap<QString, QString> { }));
    } catch (const std::exception& e) {
        if (!alive)
            co_return;
        qCDebug(lcPlayback) << "preparing the next track failed:" << e.what();
        if (generation == preparedGeneration_) {
            preparedRequestId_ = -1;
            preparedClient_.clear();
            preparedIndex_ = -1;
        }
    }
}

void PlaybackController::promotePrepared(int nextIndex, bool manual)
{
    const bool hadCurrentTrack = hasCurrentTrack();
    audioPlayer_->usePrepared(manual);
    index_ = nextIndex;
    lastKnownPositionMs_ = 0;
    preparedIndex_ = -1;
    preparedReady_ = false;
    preparedStartPending_ = true;
    ++preparedGeneration_;
    emit loadingChanged(true);
    playTimeoutTimer_->start();
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
    if (!hadCurrentTrack)
        emit currentTrackAvailabilityChanged(true);
    const QueueEntry entry = queue_[index_];
    if (waveMode_) {
        if (Rpc::RpcClient* client = sourceManager_.client(entry.sourceId)) {
            TrackStartedParams params { entry.track.id };
            Rpc::feedbackTrackStarted(*client, params).detach();
        }
    }
    emit trackChanged(entry.track, entry.sourceId);
}

void PlaybackController::next() { advance(1, /*wasSkip=*/true); }

void PlaybackController::previous() { advance(-1, /*wasSkip=*/true); }

void PlaybackController::seek(qint64 positionMs)
{
    if (!hasCurrentTrack())
        return;
    positionMs = std::clamp(positionMs, qint64(0), qint64(currentTrack().durationMs));
    audioPlayer_->seek(positionMs);
    lastKnownPositionMs_ = positionMs;
    emit positionChanged(positionMs, currentTrack().durationMs);
    emit seeked(positionMs);
}

void PlaybackController::setVolume(int volume0To100) { audioPlayer_->setVolume(volume0To100); }

void PlaybackController::setLevelsEnabled(bool enabled) { audioPlayer_->setLevelsEnabled(enabled); }

QVector<LevelReading> PlaybackController::takeLevels()
{
    return playing_ ? audioPlayer_->takeLevels() : QVector<LevelReading> { };
}

} // namespace Playback
