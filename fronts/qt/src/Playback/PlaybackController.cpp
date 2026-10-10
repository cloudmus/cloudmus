#include "PlaybackController.h"

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <stdexcept>

#include "AudioPlayer.h"
#include "Preloader.h"
#include "RpcMethods.h"
#include "StreamDownload.h"
#include "TrackCache.h"

namespace Playback {

namespace {
constexpr int kPlayTimeoutMs = 10000;
// A stream URL can be refused although it was just resolved (YouTube's
// googlevideo answers some with 403 Forbidden); a fresh one usually plays.
constexpr int kMaxStartRetries = 2;
Q_LOGGING_CATEGORY(lcPlayback, "cloudmus.playback")
}

PlaybackController::PlaybackController(Rpc::SourceManager& sourceManager, QObject* parent, const QString& trackCacheDir)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , audioPlayer_(new AudioPlayer(this))
{
    QString cacheDir = trackCacheDir;
    if (cacheDir.isEmpty()) {
        ownCacheDir_ = std::make_unique<QTemporaryDir>();
        cacheDir = ownCacheDir_->path();
    }
    trackCache_ = new TrackCache(cacheDir, this);
    preloader_ = new Preloader(*trackCache_, this);
    preloader_->setResolver(
        [this](const QString& sourceId, const QString& trackId) -> std::optional<Preloader::Resolution> {
            Rpc::RpcClient* client = sourceManager_.client(sourceId);
            if (client == nullptr || !client->available())
                return std::nullopt;
            const QJsonObject playbackCaps = client->capabilities().value(QStringLiteral("playback")).toObject();
            if (!playbackCaps.value(QStringLiteral("resolveStream")).toBool())
                return std::nullopt;
            const int id = client->allocateRequestId();
            const QPointer<Rpc::RpcClient> guard(client);
            return Preloader::Resolution { resolveStreamAsync(guard, id, trackId), [guard, id]() {
                                              if (guard && guard->available())
                                                  Rpc::playbackCancel(*guard, CancelParams { id }).detach();
                                          } };
        });
    connect(preloader_, &Preloader::ready, this, &PlaybackController::prepareNext);
    connect(preloader_, &Preloader::progressChanged, this, [this]() { updateBuffered(); });

    connect(audioPlayer_, &AudioPlayer::endOfFile, this, [this]() { advance(1, /*wasSkip=*/false); });
    connect(audioPlayer_, &AudioPlayer::started, this, [this]() {
        preparedStartPending_ = false;
        startRetries_ = 0;
        reloadOnResume_ = false;
        playTimeoutTimer_->stop();
        emit loadingChanged(false);
        playing_ = true;
        emit playingChanged(true);
        updatePreloadWindow();
    });
    connect(audioPlayer_, &AudioPlayer::failed, this,
        [this](const QString& message) { handleStartFailure(message, QStringLiteral("stream")); });
    connect(audioPlayer_, &AudioPlayer::positionChanged, this, [this](qint64 posMs, qint64 durMs) {
        lastKnownPositionMs_ = posMs;
        const bool newDuration = durMs != lastKnownDurationMs_;
        lastKnownDurationMs_ = durMs;
        emit positionChanged(posMs, durMs);
        if (newDuration)
            updateBuffered();
    });
    connect(audioPlayer_, &AudioPlayer::bufferedChanged, this, [this](qint64 bufferedMs) {
        mpvBufferedMs_ = bufferedMs;
        updateBuffered();
    });
    connect(audioPlayer_, &AudioPlayer::prepared, this, [this]() { preparedReady_ = true; });

    playTimeoutTimer_ = new QTimer(this);
    playTimeoutTimer_->setSingleShot(true);
    playTimeoutTimer_->setInterval(kPlayTimeoutMs);
    connect(playTimeoutTimer_, &QTimer::timeout, this, [this]() {
        handleStartFailure(QStringLiteral("Timed out waiting for the track to start"), QStringLiteral("timeout"));
    });
}

PlaybackController::~PlaybackController() = default;

void PlaybackController::setStreamRouteProvider(StreamRouteProvider provider)
{
    streamRouteProvider_ = std::move(provider);
    preloader_->setRouteProvider(streamRouteProvider_);
}

void PlaybackController::setPreloadDepth(int playlistTracks, int radioTracks)
{
    playlistPreloadDepth_ = qMax(0, playlistTracks);
    radioPreloadDepth_ = qMax(0, radioTracks);
    updatePreloadWindow();
}

Rpc::Task<StreamDescriptor> PlaybackController::resolveStreamAsync(
    QPointer<Rpc::RpcClient> client, int requestId, QString trackId)
{
    if (!client)
        throw std::runtime_error("the source is gone");
    ResolveStreamParams params { trackId };
    ResolveStreamResult result = co_await client->callWithId<ResolveStreamResult>(
        requestId, QStringLiteral("playback.resolveStream"), params.toJson(), 20000);
    co_return result.stream;
}

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

void PlaybackController::reportError(const QString& sourceId, const QString& reason, const QString& message)
{
    emit failed(sourceId, reason);
    emit errorOccurred(message);
}

void PlaybackController::handleStartFailure(const QString& message, const QString& reason)
{
    preparedStartPending_ = false;
    // A cached file that won't play is no use to anyone: the retry
    // downloads the track anew.
    if (hasCurrentTrack() && preloader_->isCached(currentSourceId(), currentTrack().id))
        preloader_->discard(currentSourceId(), currentTrack().id);
    if (hasCurrentTrack() && startRetries_ < kMaxStartRetries) {
        ++startRetries_;
        qCDebug(lcPlayback) << "track didn't start, resolving it again:" << message;
        playIndex(index_, /*isRetry=*/true);
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
    reportError(hasCurrentTrack() ? currentSourceId() : QString(), reason, message);
}

void PlaybackController::playAt(int index)
{
    startRetries_ = 0;
    awaitingRadioTracks_ = false;
    // Picking another track of the queue is not a skip, whichever way it
    // goes: a skip tells a radio "not this", and the station answers by
    // replacing what's queued. Where the queue stands now the station
    // learns from the picked track's feedback.trackStarted — which is also
    // what makes it send more once little is left after it.
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
        updatePreloadWindow();
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
        updatePreloadWindow();
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
        // What's already downloaded right after it stays too: it's there
        // to be played, the listener can skip it — and it's what is left to
        // listen to once the network is gone. Only as far ahead as tracks
        // are preloaded, though: the cache may hold many more of the queue
        // (played before, or fetched under a larger setting), and a "not
        // this" must not take that long to show.
        int keep = qMin(qMax(index_, startingIndex_), int(queue_.size()) - 1);
        const int furthest = keep + radioPreloadDepth_;
        while (keep >= 0 && keep < furthest && keep + 1 < queue_.size()
            && preloader_->isCached(queue_[keep + 1].sourceId, queue_[keep + 1].track.id))
            ++keep;
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
    trimRadioQueue();
    emit queueChanged();
    if (awaitingRadioTracks_ && index_ + 1 < queue_.size()) {
        awaitingRadioTracks_ = false;
        playIndex(index_ + 1);
    } else {
        updatePreloadWindow();
    }
}

void PlaybackController::playIndex(int index, bool isRetry) { playIndexAsync(index, isRetry).detach(); }

Rpc::Task<void> PlaybackController::playIndexAsync(int index, bool isRetry)
{
    if (index < 0 || index >= queue_.size())
        co_return;

    // A slow backend or the redirect preflight must not leave the old track
    // audible after the user has selected a new one.
    stopForTransition();

    // All downloaded, or on its way already: nothing to ask the source for
    // — which is also what plays with the source or the network gone. Not
    // for a retry: that's a track which just failed to start this way.
    if (!isRetry) {
        const QueueEntry cached = queue_[index];
        const bool onDisk = !preloader_->readyPath(cached.sourceId, cached.track.id).isEmpty();
        if (onDisk || preloader_->download(cached.sourceId, cached.track.id) != nullptr) {
            qCDebug(lcPlayback) << "playing" << cached.track.id << (onDisk ? "from the cache" : "from its download");
            emit loadingChanged(true);
            playTimeoutTimer_->start();
            becomeCurrent(index, cached, isRetry);
            updatePreloadWindow();
            startCurrentAudio(std::nullopt);
            co_return;
        }
    }

    Rpc::RpcClient* client = sourceManager_.client(queue_[index].sourceId);
    if (client == nullptr || !client->available()) {
        emit loadingChanged(false);
        reportError(queue_[index].sourceId, QStringLiteral("unavailable"), QStringLiteral("Source is unavailable"));
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
    // Whatever was downloading for the old track makes way now.
    updatePreloadWindow();

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
            reportError(
                entry.sourceId, QStringLiteral("rejected"), QString::fromStdString(e.error().message.toStdString()));
        }
        co_return;
    }

    if (!alive)
        co_return;

    if (transition != transitionGeneration_ || id != latestRequestId_)
        co_return;

    startingIndex_ = -1;
    becomeCurrent(index, entry, isRetry);
    if (earlyStreamReady_) {
        const StreamReadyParams ready = *earlyStreamReady_;
        earlyStreamReady_.reset();
        handleStreamReady(entry.sourceId, ready);
    }
}

void PlaybackController::becomeCurrent(int index, const QueueEntry& entry, bool isRetry)
{
    const bool wasCurrentTrack = hasCurrentTrack();
    index_ = index;
    // The queue may have grown while playback.play was in flight; the
    // trim waited for it (see trimRadioQueue()).
    trimRadioQueue();
    lastKnownPositionMs_ = 0;
    if (!wasCurrentTrack)
        emit currentTrackAvailabilityChanged(true);
    if (waveMode_) {
        Rpc::RpcClient* client = sourceManager_.client(entry.sourceId);
        if (client != nullptr && client->available()) {
            TrackStartedParams started { entry.track.id };
            Rpc::feedbackTrackStarted(*client, started).detach();
        }
    }
    if (!isRetry)
        emit trackChanged(entry.track, entry.sourceId);
    mpvBufferedMs_ = -1;
    updateBuffered(/*force=*/true);
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
    currentStream_ = params.stream;
    updatePreloadWindow();
    startCurrentAudio(params.stream);
}

void PlaybackController::startCurrentAudio(const std::optional<StreamDescriptor>& stream)
{
    // Displayed by AudioPlayer as the track identity in the OS's per-stream
    // audio widget (mpv would otherwise report itself as "mpv" playing a
    // title derived from the raw stream URL) — same "%1 — %2" convention as
    // TrayIcon::setNowPlayingTooltip.
    const QueueEntry entry = queue_[index_];
    const QString title = titleFor(entry.track);

    // Loading indicator / playTimeoutTimer_ stay active until
    // AudioPlayer::started() or failed().
    if (stream && QUrl(stream->url).isLocalFile()) {
        audioPlayer_->play(stream->url, title);
        return;
    }
    const QString file = preloader_->readyPath(entry.sourceId, entry.track.id);
    if (!file.isEmpty()) {
        audioPlayer_->play(QUrl::fromLocalFile(file).toString(), title);
        return;
    }
    if (StreamDownload* download = preloader_->download(entry.sourceId, entry.track.id)) {
        audioPlayer_->play(download, title);
        return;
    }
    if (stream) {
        // Not being downloaded (it failed before): straight from the source.
        audioPlayer_->play(stream->url, title,
            streamRouteProvider_ ? streamRouteProvider_(entry.sourceId) : std::nullopt,
            stream->headers.value_or(QMap<QString, QString> { }));
        return;
    }
    handleStartFailure(QStringLiteral("The track is no longer available"), QStringLiteral("stream"));
}

void PlaybackController::advance(int delta, bool wasSkip)
{
    startRetries_ = 0;
    if (queue_.isEmpty())
        return;
    // Next rejects the playing track; Previous doesn't — see playAt().
    if (delta > 0 || !wasSkip)
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
        if (delta == 1 && preparedReady_ && nextIndex == preparedIndex_) {
            qCDebug(lcPlayback) << "advancing to the prepared track" << nextIndex;
            promotePrepared(nextIndex, wasSkip);
        } else {
            qCDebug(lcPlayback) << "advancing to an unprepared track" << nextIndex << "prepared:" << preparedIndex_
                                << "ready:" << preparedReady_;
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
            reportError(hasCurrentTrack() ? currentSourceId() : QString(), QStringLiteral("station_empty"),
                QStringLiteral("The station sent no more tracks"));
        });
        return;
    }
    // The end of the list: a finished last track stops playback; Next
    // there has nowhere to go.
    if (!wasSkip)
        stop();
}

int PlaybackController::peekNext(int from) const
{
    const int count = int(queue_.size());
    if (from < 0 || from >= count)
        return -1;
    if (shuffleActive() && order_.size() == count) {
        const int pos = int(order_.indexOf(from));
        return pos >= 0 && pos + 1 < count ? order_[pos + 1] : -1;
    }
    if (from + 1 < count)
        return from + 1;
    return effectiveRepeatMode() == RepeatMode::All ? 0 : -1;
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
    updatePreloadWindow();
    emit playModeChanged();
}

void PlaybackController::setRepeatMode(RepeatMode mode)
{
    if (repeatMode_ == mode)
        return;
    repeatMode_ = mode;
    invalidatePrepared();
    updatePreloadWindow();
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
    if (playing_)
        prepareNext();
    else
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
    updatePreloadWindow();
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
    currentStream_.reset();
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
    preparedIndex_ = -1;
    preparedReady_ = false;
    audioPlayer_->clearPrepared();
}

void PlaybackController::trimRadioQueue()
{
    // While a track is starting, playIndexAsync() holds its index across a
    // suspension: shifting the queue now would make it land on another track.
    if (!waveMode_ || startingIndex_ >= 0 || index_ < 0)
        return;
    const int drop = qMin(int(queue_.size()) - kMaxRadioQueue, index_);
    if (drop <= 0)
        return;
    invalidatePrepared();
    queue_.remove(0, drop);
    index_ -= drop;
}

void PlaybackController::updatePreloadWindow()
{
    // While a track is starting it is already the one that matters: the
    // old one's downloads would only get in its way.
    const int head = startingIndex_ >= 0 ? startingIndex_ : index_;
    if (head < 0 || head >= queue_.size()) {
        preloader_->setWindow({ });
        return;
    }
    const auto itemFor = [this](int index) {
        const QueueEntry& entry = queue_[index];
        // A source without a network has files of its own: nothing to
        // download for them.
        const Rpc::RpcClient* client = sourceManager_.client(entry.sourceId);
        const bool local = client != nullptr && client->available() && !client->usesNetwork();
        return Preloader::Item { entry.sourceId, entry.track.id, titleFor(entry.track), std::nullopt, true, local };
    };
    QVector<Preloader::Item> items;
    Preloader::Item current = itemFor(head);
    // Its stream comes from playback.play, not from the Preloader asking.
    current.resolve = false;
    if (head == index_ && startingIndex_ < 0)
        current.stream = currentStream_;
    items.append(current);

    const int depth = waveMode_ ? radioPreloadDepth_ : playlistPreloadDepth_;
    // A source's own files are not preloaded and take nothing of `depth`.
    // Only the one that follows is kept in the window — so that it is queued
    // in mpv for a start without a gap — and the walk is bounded, as a
    // queue of them is as long as a library.
    QSet<int> seen { head };
    int networked = 0;
    int examined = 0;
    const int maxExamined = 2 * depth + 2;
    if (effectiveRepeatMode() != RepeatMode::One) {
        for (int index = peekNext(head); index >= 0 && examined < maxExamined && !seen.contains(index);
            index = peekNext(index)) {
            seen.insert(index);
            ++examined;
            const Preloader::Item item = itemFor(index);
            if (item.local) {
                if (examined == 1)
                    items.append(item);
                continue;
            }
            if (networked >= depth)
                break;
            ++networked;
            items.append(item);
        }
    }
    preloader_->setWindow(items);
    prepareNext();
}

void PlaybackController::updateBuffered(bool force)
{
    if (!hasCurrentTrack())
        return;
    const QueueEntry& entry = queue_[index_];
    // The track's own length where it says one: mpv's is still the previous
    // track's for a moment after a change.
    const qint64 durationMs = entry.track.durationMs > 0 ? qint64(entry.track.durationMs) : lastKnownDurationMs_;

    // What our own download has got — not what mpv has read ahead of the
    // playhead, which for a file on disk is a second or two however much of
    // the track is there.
    qint64 bufferedMs = mpvBufferedMs_;
    if (preloader_->isCached(entry.sourceId, entry.track.id)) {
        bufferedMs = durationMs;
    } else if (const StreamDownload* download = preloader_->download(entry.sourceId, entry.track.id)) {
        // Bytes for time: close enough for a mark on the seek bar.
        bufferedMs = download->totalLength() > 0
            ? qint64(double(durationMs) * double(download->bytesAvailable()) / double(download->totalLength()))
            : 0;
    } else if (!preloader_->readyPath(entry.sourceId, entry.track.id).isEmpty()) {
        bufferedMs = -1; // the source's own file: nothing is downloaded
    }

    // It moves with every packet; a quarter second is finer than a slider
    // pixel anyway. The end is always told, so the bar fills up whole.
    const bool reachedEnd = bufferedMs >= durationMs && shownBufferedMs_ < durationMs;
    if (!force && !reachedEnd && (bufferedMs < 0) == (shownBufferedMs_ < 0)
        && qAbs(bufferedMs - shownBufferedMs_) < 250)
        return;
    shownBufferedMs_ = bufferedMs;
    emit bufferedChanged(bufferedMs);
}

void PlaybackController::prepareNext()
{
    // Only behind a track mpv actually has: a file appended to an empty
    // playlist would start playing.
    if (!playing_ || !hasCurrentTrack() || preparedIndex_ >= 0)
        return;
    const int nextIndex = effectiveRepeatMode() == RepeatMode::One ? index_ : peekNext(index_);
    if (nextIndex < 0 || nextIndex >= queue_.size())
        return;
    const QueueEntry& entry = queue_[nextIndex];
    const QString file = preloader_->readyPath(entry.sourceId, entry.track.id);
    if (file.isEmpty())
        return;
    qCDebug(lcPlayback) << "the next track is downloaded, queued in mpv:" << nextIndex << entry.track.id;
    preparedIndex_ = nextIndex;
    audioPlayer_->prepare(QUrl::fromLocalFile(file).toString(), titleFor(entry.track));
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
    currentStream_.reset();
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
    mpvBufferedMs_ = -1;
    updateBuffered(/*force=*/true);
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
