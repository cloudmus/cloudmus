#include "Core.h"

#include <algorithm>

#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

#include "Preloader.h"
#include "ProxyRouting.h"
#include "RpcMethods.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcCore, "cloudmus.app.core")
}

Core::Core(QObject* parent)
    : QObject(parent)
    , playlistTracks_(QFileInfo(settings_.filePath()).absolutePath() + QStringLiteral("/playlist-tracks"))
    , analytics_(settings_)
    , playback_(sourceManager_, nullptr,
          QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cloudmus/tracks"))
    , preload_(playback_, settings_)
    , outbox_(sourceManager_, QFileInfo(settings_.filePath()).absolutePath() + QStringLiteral("/outbox.json"))
    , audioPulse_(playback_)
    , sourceSession_(sourceManager_, playback_, authStates_, trackStates_, coverArtCache_, messages_)
    , downloads_(sourceManager_, sourceSession_, trackStates_, coverArtCache_, settings_, messages_)
    , nowPlaying_(playback_, sourceManager_, trackStates_, playbackHistory_, settings_, downloads_, messages_)
    , playlistEditing_(sourceManager_, messages_)
    , sources_(sourceManager_, sourceSession_, authStates_, playlistEditing_, coverArtCache_, settings_, messages_)
    , activePlaylist_(playback_, sourceManager_, trackStates_, coverArtCache_, playbackHistory_, settings_, sources_,
          playlistEditing_, messages_, playlistTracks_)
    , activeTracks_(activePlaylist_, playback_, trackStates_, preload_, coverArtCache_)
    , trackActions_(sourceManager_, playback_, activePlaylist_, downloads_, playlistEditing_)
    , sourcePage_(sourceSession_)
    , listeningTime_(playback_, analytics_)
    , connectionStatus_(connectivity_, sourceManager_, playback_)
{
    // The language first: whatever is created or spawned below already
    // speaks it.
    translator_.setLanguage(settings_.language());
    sourceManager_.setLocale(translator_.language());
    // A later switch (the Settings dialog) reaches the running backends too.
    connect(&translator_, &I18n::Translator::languageChanged, &sourceManager_, &Rpc::SourceManager::setLocale);

    // Signed out: its playlists are not the user's any more, nor what was
    // still to be sent for them.
    connect(&authStates_, &Rpc::AuthStates::signedOut, this, [this](const QString& sourceId) {
        playlistTracks_.removeSource(sourceId);
        outbox_.removeSource(sourceId);
    });
    // A source that stops leaves nothing to stream the rest of its track from.
    connect(&sourceManager_, &Rpc::SourceManager::sourceStopped, this, [this](const QString& sourceId) {
        if (playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId)
            playback_.dropCurrentTrack();
    });
    // Every track that starts playing goes into History, and its "last
    // played" with it.
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            playbackHistory_.record(sourceId, track);
            trackStates_.setLastPlayed(sourceId, track.id, QDateTime::currentDateTimeUtc());
            analytics_.recordPlayback(sourceId);
        });
    connect(&downloads_, &ViewModel::Downloads::completed, &analytics_, &Analytics::recordDownload);
    connect(&playlistEditing_, &PlaylistEditing::playlistEdited, &analytics_,
        [this](const QString& sourceId, const Track&, const QString&, bool added, int) {
            analytics_.recordPlaylistChange(sourceId, added);
        });
    connect(&playback_, &Playback::PlaybackController::streamResolved, this,
        [this](const QString& sourceId, const QString& trackId, const StreamDescriptor& stream) {
            trackStates_.setStreamQuality(
                sourceId, trackId, { stream.codec.value_or(QString()), stream.bitrateKbps.value_or(0) });
        });
    // A track played from the cache is never resolved, so its source never
    // says what it is: the file itself does.
    connect(&trackStates_, &Library::TrackStates::streamQualityWanted, this,
        [this](const QString& sourceId, const QString& trackId) {
            const QString file = playback_.preloader().cachedFile(sourceId, trackId);
            if (file.isEmpty() || probedFiles_.contains(file))
                return;
            probedFiles_.insert(file);
            fileProbe_.probe(file, [this, sourceId, trackId](const Playback::AudioFileProbe::Info& info) {
                trackStates_.setStreamQuality(sourceId, trackId, { info.codec, info.bitrateKbps });
            });
        });
    connect(&playback_, &Playback::PlaybackController::trackChanged, &trackStates_,
        [this](const Track& track, const QString& sourceId) { trackStates_.requestStreamQuality(sourceId, track.id); });
    connect(&playback_, &Playback::PlaybackController::errorOccurred, &messages_,
        [this](const QString& text) { messages_.error(text); });
    connect(&playback_, &Playback::PlaybackController::offlineNotice, &messages_,
        [this](const QString& text) { messages_.offline(text); });
    // Each time it gets worse, and once at the start: the view may only be
    // coming up then, hence later.
    const auto warnOfSpace = [this]() {
        const QString warning = preload_.spaceWarning();
        if (!warning.isEmpty())
            messages_.error(warning);
    };
    connect(&preload_, &ViewModel::Preload::spaceWarningChanged, this, warnOfSpace);
    QTimer::singleShot(0, this, warnOfSpace);

    // The network: what says it's gone, what says it's there, and going over
    // the sources again until it is. A listing that works gets the rest going
    // (ActivePlaylist: what's still to load, the station, the preloads).
    const auto reportOffline = [this](const QString& sourceId) { connectivity_.reportOffline(sourceId); };
    connect(&playback_, &Playback::PlaybackController::offlineSeen, &connectivity_, reportOffline);
    connect(&playback_.preloader(), &Playback::Preloader::offlineSeen, &connectivity_, reportOffline);
    connect(&playback_.preloader(), &Playback::Preloader::failed, &connectivity_,
        [this](const QString& sourceId, const QString&) { connectivity_.suspect(sourceId); });
    connect(&sources_, &ViewModel::Sources::listingFailedWith, &connectivity_,
        [this](const QString& sourceId, bool offline) {
            if (offline)
                connectivity_.reportOffline(sourceId);
            else
                connectivity_.suspect(sourceId);
        });
    connect(&sources_, &ViewModel::Sources::listingSucceeded, &connectivity_, [this](const QString& sourceId) {
        // A source with only local files says nothing about the network.
        const Rpc::RpcClient* client = sourceManager_.client(sourceId);
        if (client != nullptr && client->usesNetwork())
            connectivity_.reportReached(sourceId);
    });
    connect(&connectivity_, &Connectivity::retryWanted, &sources_, &ViewModel::Sources::refresh);
    connect(&sourceSession_, &SourceSession::connectionChanged, &connectivity_,
        [this](const QString& sourceId, bool online, const QString& detail) {
            if (online)
                connectivity_.reportReached(sourceId);
            else
                connectivity_.reportOffline(sourceId, detail);
        });
    connectivity_.setChecker([this](QString sourceId) { return checkConnection(std::move(sourceId)); });
    connectivity_.setSourcesProvider([this]() {
        QStringList ids;
        for (const Rpc::RpcClient* client : sourceManager_.clients()) {
            if (client->usesNetwork())
                ids.append(client->sourceId());
        }
        return ids;
    });
    // Back: what failed for lack of it is asked for again — the playlists,
    // and with them (ActivePlaylist) the tracks, a radio's station, the
    // preloads.
    connect(&connectivity_, &Connectivity::sourceOnline, &sources_, &ViewModel::Sources::refresh);
    setUpOutbox();
    // A radio with no network goes on with what's downloaded; a track that
    // isn't, from a service found offline, waits for a fresh check first.
    activePlaylist_.setOfflineProvider([this](const QString& sourceId) { return connectivity_.isOffline(sourceId); });
    playback_.setOfflineRadioProvider(
        [this](const QString& sourceId, int count) { return activePlaylist_.cachedRadioTracks(sourceId, count); });
    playback_.setReachableCheck([this](QString sourceId) { return reachable(std::move(sourceId)); });
    // The sidebar marks each service that can't be reached.
    connect(&connectivity_, &Connectivity::stateChanged, this, [this](const QString& sourceId) {
        sources_.model().setSourceOffline(sourceId, connectivity_.isOffline(sourceId));
    });
    connect(&playback_, &Playback::PlaybackController::trackUnavailable, &trackStates_,
        &Library::TrackStates::setUnavailable);
    connect(&playback_, &Playback::PlaybackController::trackAvailableAgain, &trackStates_,
        &Library::TrackStates::clearUnavailable);
    // A stream that resolved is proof enough.
    connect(&playback_, &Playback::PlaybackController::streamResolved, &trackStates_,
        [this](const QString& sourceId, const QString& trackId, const StreamDescriptor&) {
            trackStates_.clearUnavailable(sourceId, trackId);
        });

    // What goes wrong, and the sign-in and like steps, for statistics.
    connect(&playback_, &Playback::PlaybackController::failed, &analytics_, &Analytics::recordPlaybackFailure);
    connect(&sourceManager_, &Rpc::SourceManager::sourceCrashed, &analytics_,
        [this](const QString& sourceId) { analytics_.recordBackendFailure(sourceId, false); });
    connect(&sourceManager_, &Rpc::SourceManager::sourceUnavailable, &analytics_,
        [this](const QString& sourceId) { analytics_.recordBackendFailure(sourceId, true); });
    connect(&authStates_, &Rpc::AuthStates::signInPrompted, &analytics_,
        [this](const QString& sourceId) { analytics_.recordSignIn(sourceId, Analytics::SignInStep::Prompt); });
    connect(&authStates_, &Rpc::AuthStates::signInCompleted, &analytics_,
        [this](const QString& sourceId) { analytics_.recordSignIn(sourceId, Analytics::SignInStep::Success); });
    connect(&authStates_, &Rpc::AuthStates::signInFailed, &analytics_,
        [this](const QString& sourceId) { analytics_.recordSignIn(sourceId, Analytics::SignInStep::Error); });
    connect(&nowPlaying_, &ViewModel::NowPlaying::feedbackSent, &analytics_, &Analytics::recordTrackFeedback);
    const auto syncPreview = [this]() {
        const std::optional<Playback::QueueEntry> entry = activePlaylist_.savedEntry();
        nowPlaying_.setPreviewTrack(entry ? entry->sourceId : QString(), entry ? entry->track.id : QString());
    };
    connect(&activePlaylist_, &ViewModel::ActivePlaylist::contextChanged, this, syncPreview);
    connect(&activePlaylist_, &ViewModel::ActivePlaylist::entriesChanged, this, syncPreview);
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, syncPreview);
    syncPreview();
    connect(&nowPlaying_, &ViewModel::NowPlaying::resumeRequested, &activePlaylist_, &ViewModel::ActivePlaylist::resume);

    // History's saved snapshots: when each track was last played, plus
    // whatever like state it was recorded with — only where nothing
    // fresher is known (a source's own report always wins, see observe()).
    for (const History::HistoryEntry& e : playbackHistory_.entries()) {
        trackStates_.setLastPlayed(e.sourceId, e.track.id, e.playedAt);
        trackStates_.observe(e.sourceId, e.track, /*onlyIfUnknown=*/true);
        coverArtCache_.assignSource(e.sourceId, { e.track });
    }

    sourceManager_.setDisabledIds(settings_.disabledSources());
    // Each backend reaches the network the way its Settings page says:
    // through a proxy, directly, or as the environment has it.
    sourceManager_.setEnvironmentProvider(
        [this](const QString& sourceId) { return Net::backendEnvironment(connectionOf(sourceId)); });
    // ...and so do the stream URLs and covers it hands out, which the
    // front fetches.
    coverArtCache_.setProxyProvider(
        [this](const QString& sourceId, const QUrl& url) { return Net::networkProxy(connectionOf(sourceId), url); });
    // Covers are fetched along with the tracks preloaded, so a track that
    // comes up has its cover at hand: the Windows notification, which
    // can't be updated once shown, would otherwise often go out without it.
    connect(&playback_, &Playback::PlaybackController::preloadWindowChanged, this,
        [this](const QVector<Playback::QueueEntry>& entries) {
            for (const Playback::QueueEntry& entry : entries) {
                const Track& track = entry.track;
                coverArtCache_.request(
                    track.coverUrl.value_or(track.album ? track.album->coverUrl.value_or(QString()) : QString()));
            }
        });
    nowPlaying_.setCovers(&coverArtCache_);
    // Tracks are saved from the playback cache, or fetched for it.
    downloads_.setPreloader(&playback_.preloader());
    playback_.setStreamRouteProvider([this](const QString& sourceId) -> std::optional<QNetworkProxy> {
        const Net::Connection connection = connectionOf(sourceId);
        // System still routes the stream through the environment's proxy
        // when it names one: the backend resolved the URL through it.
        if (connection.mode == Net::Connection::Mode::System)
            return Net::environmentProxy(QUrl());
        return Net::networkProxy(connection, QUrl());
    });
    playback_.setDownloadConnectionsProvider(
        [this](const QString& sourceId) { return settings_.sourceDownloadConnections(sourceId); });
}

Net::Connection Core::connectionOf(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client != nullptr && client->available() && !client->usesNetwork())
        return { };
    return Net::connectionFor(settings_, sourceId);
}

void Core::setUpOutbox()
{
    outbox_.setOfflineProvider([this](const QString& sourceId) { return connectivity_.isOffline(sourceId); });
    nowPlaying_.setOutbox(&outbox_);
    playlistEditing_.setOutbox(&outbox_);
    // What a playlist was saved holding, for "Add to playlist" with no network.
    playlistEditing_.setSavedMembership(
        [this](const QString& sourceId, const QString& trackId, const QList<Playlist>& playlists) {
            QStringList containing;
            for (const Playlist& playlist : playlists) {
                const auto saved = playlistTracks_.load(sourceId, playlist.id);
                if (saved && std::any_of(saved->cbegin(), saved->cend(), [&](const Playback::QueueEntry& e) {
                        return e.track.id == trackId;
                    }))
                    containing.append(playlist.id);
            }
            return containing;
        });
    // And what's saved of the playlist follows an edit, so that it opens
    // with the change when there's no network to list it again.
    connect(&playlistEditing_, &PlaylistEditing::playlistEdited, this,
        [this](const QString& sourceId, const Track& track, const QString& playlistId, bool added, int) {
            auto saved = playlistTracks_.load(sourceId, playlistId);
            if (!saved)
                return;
            const auto it = std::find_if(
                saved->begin(), saved->end(), [&](const Playback::QueueEntry& e) { return e.track.id == track.id; });
            if (added && it == saved->end())
                saved->append(Playback::QueueEntry { sourceId, track });
            else if (!added && it != saved->end())
                saved->erase(it);
            else
                return;
            playlistTracks_.save(sourceId, playlistId, *saved);
        });
    // Sent once the source can be reached again: back online, or (re)started.
    connect(&connectivity_, &Connectivity::sourceOnline, this,
        [this](const QString& sourceId) { outbox_.flush(sourceId).detach(); });
    connect(&sourceManager_, &Rpc::SourceManager::sourceReady, this, [this](Rpc::RpcClient* client) {
        if (!connectivity_.isOffline(client->sourceId()))
            outbox_.flush(client->sourceId()).detach();
    });
    // A radio's feedback, kept while the source can't be told — only then:
    // it steers the station now, and a like being sent mustn't make it late.
    playback_.setFeedbackHold([this](const QString& sourceId) {
        const Rpc::RpcClient* client = sourceManager_.client(sourceId);
        return connectivity_.isOffline(sourceId) || client == nullptr || !client->available();
    });
    connect(&playback_, &Playback::PlaybackController::feedbackDeferred, &outbox_,
        [this](const QString& sourceId, const QString& method, const QJsonObject& params) {
            outbox_.enqueue(sourceId, method, params);
        });
    // A play outside a radio, for the service's history — later, if need be.
    connect(&playback_, &Playback::PlaybackController::trackPlayed, this,
        [this](const QString& sourceId, const Track& track, int playedMs, const QDateTime& startedAt) {
            reportPlay(sourceId, track.id, playedMs, startedAt).detach();
        });
    connect(&outbox_, &Outbox::offlineSeen, this,
        [this](const QString& sourceId) { connectivity_.reportOffline(sourceId); });
    connect(&outbox_, &Outbox::countChanged, &connectionStatus_, &ViewModel::ConnectionStatus::setPendingActions);
    connectionStatus_.setPendingActions(outbox_.count());
    // A like held over what listings say (see TrackStates::holdRating())
    // until the source has it.
    const auto settled = [this](const Outbox::Item& item) {
        if (!item.key.startsWith(QStringLiteral("rating:")) || outbox_.contains(item.sourceId, item.key))
            return;
        trackStates_.holdRating(item.sourceId, item.params.value(QStringLiteral("trackId")).toString(), false);
    };
    connect(&outbox_, &Outbox::delivered, this, settled);
    connect(&outbox_, &Outbox::dropped, this, [this, settled](const Outbox::Item& item, const QString& error) {
        settled(item);
        qCWarning(lcCore) << "gave up sending" << item.method << "to" << item.sourceId << ":" << error;
    });
    // What waits from the last run still wins over the listings.
    for (const Outbox::Item& item : outbox_.items()) {
        if (item.key.startsWith(QStringLiteral("rating:"))) {
            const QString trackId = item.params.value(QStringLiteral("trackId")).toString();
            trackStates_.holdRating(item.sourceId, trackId, true);
            const QString method = item.method;
            if (method == QStringLiteral("feedback.like") || method == QStringLiteral("feedback.unlike"))
                trackStates_.setLiked(item.sourceId, trackId, method == QStringLiteral("feedback.like"));
            else
                trackStates_.setDisliked(item.sourceId, trackId, method == QStringLiteral("feedback.dislike"));
        }
    }
}

Rpc::Task<bool> Core::reachable(QString sourceId)
{
    if (!connectivity_.isOffline(sourceId))
        co_return true;
    co_return co_await connectivity_.checkSource(std::move(sourceId));
}

Rpc::Task<void> Core::reportPlay(QString sourceId, QString trackId, int playedMs, QDateTime startedAt)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr
        || !client->capabilities()
            .value(QStringLiteral("feedback"))
            .toObject()
            .value(QStringLiteral("played"))
            .toBool())
        co_return;
    PlayedParams params { trackId, playedMs, startedAt.toString(Qt::ISODate) };
    if (outbox_.holds(sourceId)) {
        params.deferred = true;
        outbox_.enqueue(sourceId, QStringLiteral("feedback.played"), params.toJson());
        co_return;
    }
    const QPointer<Outbox> outbox(&outbox_);
    try {
        co_await Rpc::feedbackPlayed(*client, params);
    } catch (const Rpc::RpcCallException& e) {
        if (outbox && e.isOffline()) {
            params.deferred = true;
            outbox_.enqueue(sourceId, QStringLiteral("feedback.played"), params.toJson());
        } else {
            qCDebug(lcCore) << "feedback.played failed for" << trackId << ":" << e.what();
        }
    }
}

Rpc::Task<std::optional<Connectivity::CheckResult>> Core::checkConnection(QString sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available()
        || !client->capabilities().value(QStringLiteral("connectionCheck")).toBool())
        co_return std::nullopt;
    const QPointer<Rpc::RpcClient> guard(client);
    try {
        const ConnectionState state = co_await Rpc::connectionCheck(*client);
        co_return Connectivity::CheckResult { state.state == QStringLiteral("online"),
            state.detail.value_or(QString()) };
    } catch (const Rpc::RpcCallException& e) {
        // No answer in time is a source too busy to say, not the service gone.
        if (e.isOffline())
            co_return Connectivity::CheckResult { false, e.error().message };
        co_return std::nullopt;
    }
}

} // namespace App
