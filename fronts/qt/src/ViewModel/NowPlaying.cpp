#include "NowPlaying.h"

#include <QDir>
#include <QJsonObject>
#include <QLoggingCategory>

#include "Downloads.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "Settings.h"
#include "SourceManager.h"
#include "TrackStates.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcNowPlaying, "cloudmus.viewmodel.nowplaying")
}

NowPlaying::NowPlaying(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, History::PlaybackHistory& history, Config::Settings& settings,
    Downloads& downloads, Messages& messages, QObject* parent)
    : QObject(parent)
    , playback_(playback)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , history_(history)
    , settings_(settings)
    , downloads_(downloads)
    , messages_(messages)
{
    // The saved volume and play modes, applied to playback itself — mpv
    // otherwise starts at its own default (max) volume.
    volume_ = settings_.volume();
    playback_.setVolume(volume_);
    playback_.setShuffle(settings_.shuffle());
    playback_.setRepeatMode(settings_.repeatMode());

    connect(&playback_, &Playback::PlaybackController::trackChanged, this, [this]() {
        resetBusy();
        emit trackChanged();
        emit feedbackChanged();
    });
    connect(&playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, this, [this](bool available) {
        if (available)
            return; // trackChanged() follows right away
        resetBusy();
        emit trackChanged();
        emit feedbackChanged();
    });
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, &NowPlaying::playingChanged);
    connect(&playback_, &Playback::PlaybackController::loadingChanged, this, [this](bool loading) {
        loading_ = loading;
        emit loadingChanged(loading);
    });
    connect(&playback_, &Playback::PlaybackController::positionChanged, this, &NowPlaying::positionChanged);
    connect(&playback_, &Playback::PlaybackController::queueAvailabilityChanged, this,
        &NowPlaying::queueAvailabilityChanged);
    // However they were changed — here, or over MPRIS — play modes are
    // remembered as the user set them.
    connect(&playback_, &Playback::PlaybackController::playModeChanged, this, [this]() {
        settings_.setShuffle(playback_.shuffle());
        settings_.setRepeatMode(playback_.repeatMode());
        emit playModesChanged();
    });
    connect(
        &trackStates_, &Library::TrackStates::changed, this, [this](const QString& sourceId, const QString& trackId) {
            if (isCurrent(sourceId, trackId))
                emit feedbackChanged();
        });
    connect(&trackStates_, &Library::TrackStates::bulkChanged, this, &NowPlaying::feedbackChanged);
    // Whether the playing track is being saved.
    connect(&downloads_, &Downloads::changed, this, &NowPlaying::feedbackChanged);
}

bool NowPlaying::hasTrack() const { return playback_.hasCurrentTrack(); }

const Track& NowPlaying::track() const { return playback_.currentTrack(); }

QString NowPlaying::sourceId() const { return hasTrack() ? playback_.currentSourceId() : QString(); }

QString NowPlaying::webUrl() const { return hasTrack() ? track().webUrl.value_or(QString()) : QString(); }

bool NowPlaying::playing() const { return playback_.isPlaying(); }

bool NowPlaying::queueAvailable() const { return playback_.hasQueue(); }

void NowPlaying::togglePause() { playback_.togglePause(); }

void NowPlaying::next() { playback_.next(); }

void NowPlaying::previous() { playback_.previous(); }

void NowPlaying::stop() { playback_.stop(); }

void NowPlaying::seek(qint64 positionMs) { playback_.seek(positionMs); }

void NowPlaying::setVolume(int volume0To100)
{
    if (volume_ == volume0To100)
        return;
    volume_ = volume0To100;
    playback_.setVolume(volume_);
    settings_.setVolume(volume_);
    emit volumeChanged(volume_);
}

bool NowPlaying::shuffle() const { return playback_.shuffleActive(); }

Playback::RepeatMode NowPlaying::repeatMode() const { return playback_.effectiveRepeatMode(); }

bool NowPlaying::isRadio() const { return playback_.isRadio(); }

void NowPlaying::setShuffle(bool on) { playback_.setShuffle(on); }

void NowPlaying::setRepeatMode(Playback::RepeatMode mode) { playback_.setRepeatMode(mode); }

NowPlaying::Feedback NowPlaying::feedback() const
{
    Feedback result;
    if (!hasTrack())
        return result;
    const QString source = playback_.currentSourceId();
    const QJsonObject caps = capabilities(source);
    const QJsonObject feedback = caps.value(QStringLiteral("feedback")).toObject();
    const Library::TrackState state = trackStates_.state(source, track().id);
    result.likeSupported = feedback.value(QStringLiteral("like")).toBool();
    result.liked = pendingLiked_.value_or(state.liked.value_or(false));
    result.likeBusy = pendingLiked_.has_value();
    result.dislikeSupported = feedback.value(QStringLiteral("dislike")).toBool();
    result.disliked = pendingDisliked_.value_or(state.disliked.value_or(false));
    result.dislikeBusy = pendingDisliked_.has_value();
    result.downloadSupported = downloads_.isEnabled() && caps.value(QStringLiteral("download")).toBool();
    result.downloadBusy = downloads_.isDownloading(source, track().id);
    result.playlistsSupported
        = caps.value(QStringLiteral("browse")).toObject().value(QStringLiteral("editPlaylists")).toBool();
    return result;
}

void NowPlaying::setLiked(bool liked)
{
    if (hasTrack())
        setTrackLiked(playback_.currentSourceId(), track().id, liked, /*announce=*/false).detach();
}

void NowPlaying::setDisliked(bool disliked)
{
    if (hasTrack())
        setTrackDisliked(playback_.currentSourceId(), track().id, disliked, /*announce=*/false).detach();
}

void NowPlaying::download()
{
    if (hasTrack())
        downloads_.downloadTrack(playback_.currentSourceId(), track());
}

Rpc::Task<void> NowPlaying::setTrackLiked(QString sourceId, QString trackId, bool liked, bool announce)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available())
        co_return;
    // Busy shows only on the current track — the queue may move on while
    // the call runs; what the call changes (trackStates_, history) is
    // updated regardless.
    if (isCurrent(sourceId, trackId)) {
        pendingLiked_ = liked;
        emit feedbackChanged();
    }
    try {
        if (liked)
            co_await Rpc::feedbackLike(*client, LikeParams { trackId });
        else
            co_await Rpc::feedbackUnlike(*client, UnlikeParams { trackId });
        if (isCurrent(sourceId, trackId))
            pendingLiked_.reset();
        // Cross-clears the dislike too (docs/protocol.md §7.4).
        trackStates_.setLiked(sourceId, trackId, liked);
        history_.markTrackLiked(sourceId, trackId, liked); // keeps the saved snapshot fresh
        if (announce)
            messages_.info(liked ? tr("Added to Liked") : tr("Removed from Liked"));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcNowPlaying) << "feedback.like/unlike failed for" << trackId << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
        // Back to what trackStates_ still says.
        if (isCurrent(sourceId, trackId)) {
            pendingLiked_.reset();
            emit feedbackChanged();
        }
    }
}

Rpc::Task<void> NowPlaying::setTrackDisliked(QString sourceId, QString trackId, bool disliked, bool announce)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available())
        co_return;
    if (isCurrent(sourceId, trackId)) {
        pendingDisliked_ = disliked;
        emit feedbackChanged();
    }
    try {
        if (disliked)
            co_await Rpc::feedbackDislike(*client, DislikeParams { trackId });
        else
            co_await Rpc::feedbackUndislike(*client, UndislikeParams { trackId });
        const bool wasCurrent = isCurrent(sourceId, trackId);
        if (wasCurrent)
            pendingDisliked_.reset();
        // Cross-clears the like too (docs/protocol.md §7.4).
        trackStates_.setDisliked(sourceId, trackId, disliked);
        if (disliked)
            history_.markTrackLiked(sourceId, trackId, false);
        // No point listening to a track just disliked — move on, like the
        // services' own players do. next() also sends the skip feedback a
        // radio uses to adapt its upcoming tracks.
        if (wasCurrent && disliked)
            playback_.next();
        if (announce)
            messages_.info(disliked ? tr("Disliked") : tr("Removed dislike"));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcNowPlaying) << "feedback.dislike/undislike failed for" << trackId << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
        if (isCurrent(sourceId, trackId)) {
            pendingDisliked_.reset();
            emit feedbackChanged();
        }
    }
}

bool NowPlaying::isCurrent(const QString& sourceId, const QString& trackId) const
{
    return hasTrack() && playback_.currentSourceId() == sourceId && track().id == trackId;
}

QJsonObject NowPlaying::capabilities(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr ? client->capabilities() : QJsonObject();
}

void NowPlaying::resetBusy()
{
    pendingLiked_.reset();
    pendingDisliked_.reset();
}

} // namespace ViewModel
