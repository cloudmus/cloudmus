#include "Core.h"

#include "ProxyRouting.h"

namespace App {

Core::Core(QObject* parent)
    : QObject(parent)
    , analytics_(settings_)
    , playback_(sourceManager_)
    , sourceSession_(sourceManager_, playback_, authStates_, trackStates_, coverArtCache_, messages_)
    , downloads_(sourceManager_, sourceSession_, trackStates_, coverArtCache_, settings_, messages_)
    , nowPlaying_(playback_, sourceManager_, trackStates_, playbackHistory_, settings_, downloads_, messages_)
    , hotkeys_(settings_)
    , hotkeyDispatcher_(nowPlaying_, hotkeys_)
    , playlistEditing_(sourceManager_, messages_)
    , sources_(sourceManager_, sourceSession_, authStates_, playlistEditing_, coverArtCache_, settings_, messages_)
    , activePlaylist_(playback_, sourceManager_, trackStates_, coverArtCache_, playbackHistory_, settings_, sources_,
          playlistEditing_, messages_)
    , browse_(sourceManager_, trackStates_, coverArtCache_, playbackHistory_, settings_, sources_, activePlaylist_,
          playlistEditing_, messages_)
    , sourcePage_(sourceSession_)
    , updates_(settings_)
{
    // The language first: whatever is created or spawned below already
    // speaks it.
    translator_.setLanguage(settings_.language());
    sourceManager_.setLocale(translator_.language());
    // A later switch (the Settings dialog) reaches the running backends too.
    connect(&translator_, &I18n::Translator::languageChanged, &sourceManager_, &Rpc::SourceManager::setLocale);

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
    connect(&playback_, &Playback::PlaybackController::errorOccurred, &messages_, &ViewModel::Messages::error);
    const auto syncPreview = [this]() {
        const std::optional<Playback::QueueEntry> entry = activePlaylist_.savedEntry();
        nowPlaying_.setPreviewTrack(entry ? entry->sourceId : QString(), entry ? entry->track.id : QString());
    };
    connect(&activePlaylist_, &ViewModel::ActivePlaylist::contextChanged, this, syncPreview);
    connect(&activePlaylist_, &ViewModel::ActivePlaylist::entriesChanged, this, syncPreview);
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, syncPreview);
    syncPreview();

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
    playback_.setStreamRouteProvider([this](const QString& sourceId) -> std::optional<QNetworkProxy> {
        const Net::Connection connection = connectionOf(sourceId);
        // System still routes the stream through the environment's proxy
        // when it names one: the backend resolved the URL through it.
        if (connection.mode == Net::Connection::Mode::System)
            return Net::environmentProxy(QUrl());
        return Net::networkProxy(connection, QUrl());
    });
}

Net::Connection Core::connectionOf(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client != nullptr && client->available() && !client->usesNetwork())
        return { };
    return Net::connectionFor(settings_, sourceId);
}

} // namespace App
