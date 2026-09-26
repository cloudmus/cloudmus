#include "Core.h"

#include "ProxyRouting.h"

namespace App {

Core::Core(QObject* parent)
    : QObject(parent)
    , playback_(sourceManager_)
    , sourceSession_(sourceManager_, playback_, authStates_, trackStates_, coverArtCache_, messages_)
    , nowPlaying_(playback_, sourceManager_, trackStates_, playbackHistory_, settings_, messages_)
    , playlistEditing_(sourceManager_, messages_)
{
    // Every track that starts playing goes into History, and its "last
    // played" with it.
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            playbackHistory_.record(sourceId, track);
            trackStates_.setLastPlayed(sourceId, track.id, QDateTime::currentDateTimeUtc());
        });
    connect(&playback_, &Playback::PlaybackController::errorOccurred, &messages_, &ViewModel::Messages::error);

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
        [this](const QString& sourceId) { return Net::backendEnvironment(Net::connectionFor(settings_, sourceId)); });
    // ...and so do the stream URLs and covers it hands out, which the
    // front fetches.
    coverArtCache_.setProxyProvider([this](const QString& sourceId, const QUrl& url) {
        return Net::networkProxy(Net::connectionFor(settings_, sourceId), url);
    });
    playback_.setStreamRouteProvider([this](const QString& sourceId) -> std::optional<QNetworkProxy> {
        const Net::Connection connection = Net::connectionFor(settings_, sourceId);
        if (connection.mode == Net::Connection::Mode::System)
            return std::nullopt;
        return Net::networkProxy(connection, QUrl());
    });
}

} // namespace App
